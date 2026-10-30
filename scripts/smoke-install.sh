#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Install smoke test, runs INSIDE a fresh distribution container (see linux-smoke.yml):
#   smoke-install.sh rpm|deb|tgz <package-dir> <out-dir> <image>   (EXPECT_QT_VERSION: version the packages bundle)
# Installs the packages with the distribution package manager (dependencies come from the
# distribution repositories), then runs `dofscroll --version`, `dofscroll --help` and, if the
# package contains it, `dofscroll-ui --version` headless (QT_QPA_PLATFORM=offscreen, no display).
# The packages bundle their own Qt (usr/lib/dofscroll): the test checks that the interface loads Qt
# from there, that no Qt of the distribution was installed (nothing pulled it in), that the self-test
# runs and that the X11 and Wayland platform plugins load. "tgz" unpacks the archive into a folder and
# needs nothing installed but the base image (the daemon is not run: it needs libspnav).
# Writes a one-line verdict to <out-dir>/summary.md and exits non-zero on any failure.
set -u
kind="$1"; pkgdir="$2"; out="$3"; image="$4"
log="$out/install.log"
mkdir -p "$out"; : > "$log"

fail() {
    echo "FAILED: $1"
    echo "| $image | $kind | FAILED | $1 |" > "$out/summary.md"
    exit 1
}

case "$kind" in
rpm)
    # EPEL carries spacenavd/libspnav on the RHEL family; Fedora has no epel-release (ignored).
    dnf install -y epel-release >>"$log" 2>&1 || echo "note: no epel-release (fine on Fedora)"
    dnf install -y dnf-plugins-core >>"$log" 2>&1 || true
    dnf config-manager --set-enabled crb >>"$log" 2>&1 || true
    set -- "$pkgdir"/dofscroll-*.rpm
    [ -f "$1" ] || fail "no RPM found in $pkgdir"
    dnf install -y "$@" >>"$log" 2>&1
    rc=$?
    ;;
deb)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >>"$log" 2>&1
    set -- "$pkgdir"/dofscroll*.deb
    [ -f "$1" ] || fail "no DEB found in $pkgdir"
    apt-get install -y --no-install-recommends "$@" >>"$log" 2>&1
    rc=$?
    ;;
tgz)
    set -- "$pkgdir"/dofscroll-*-Linux.tar.gz
    [ -f "$1" ] || fail "no TGZ found in $pkgdir"
    mkdir -p /opt/dofscroll-tgz && tar xzf "$1" -C /opt/dofscroll-tgz || fail "cannot unpack $1"
    PATH="$(echo /opt/dofscroll-tgz/dofscroll-*-Linux/usr/bin):$PATH"; export PATH
    rc=0
    ;;
*) fail "unknown kind $kind" ;;
esac

if [ "$rc" -ne 0 ]; then
    echo "--- install log (tail) ---"; tail -n 30 "$log"
    # First line that names the problem (dnf: "nothing provides", apt: "Depends:")
    err="$(grep -i -m1 -E 'nothing provides|no match for argument|unmet dependencies|depends:|unable to locate|conflicts|error' "$log" | tr '|' '/' | cut -c1-200)"
    fail "install: ${err:-see log}"
fi
echo "installed ok"

ver="TGZ"
if [ "$kind" != tgz ]; then
    command -v dofscroll >/dev/null 2>&1 || fail "dofscroll not on PATH after install"
    ver="$(dofscroll --version 2>&1)" || fail "dofscroll --version exited non-zero: $ver"
    echo "dofscroll --version: $ver"
    dofscroll --help >/dev/null 2>&1 || fail "dofscroll --help exited non-zero"
fi

ui="no UI"
if command -v dofscroll-ui >/dev/null 2>&1; then
    uibin="$(command -v dofscroll-ui)"
    export LC_ALL=C.UTF-8
    # 1. No Qt of the distribution came with the package (it must not need one).
    case "$kind" in
    rpm) rpm -qa 'qt6-qtbase*' | grep . && fail "the package pulled in a system Qt: $(rpm -qa 'qt6-qtbase*' | tr '\n' ' ')" ;;
    deb) dpkg -l 'libqt6core6*' 2>/dev/null | grep '^ii' && fail "the package pulled in a system Qt" ;;
    esac
    # 2. Qt, ICU and OpenGL come from the private directory, nothing is unresolved.
    ldd "$uibin" >"$out/ldd.txt" 2>&1
    grep -q 'not found' "$out/ldd.txt" && { grep 'not found' "$out/ldd.txt"; fail "dofscroll-ui has unresolved libraries: $(grep 'not found' "$out/ldd.txt" | head -3 | tr -s ' \t\n|' ' ')"; }
    grep -q 'libQt6Core.so.6 => .*/lib/dofscroll/lib/' "$out/ldd.txt" || { cat "$out/ldd.txt"; fail "libQt6Core is not loaded from lib/dofscroll"; }
    if grep -E 'libQt6|libicu' "$out/ldd.txt" | grep -v '/lib/dofscroll/lib/'; then fail "Qt or ICU is loaded from outside lib/dofscroll"; fi
    # 3. The version line, and the Qt that runs is the bundled one.
    QT_QPA_PLATFORM=offscreen dofscroll-ui --version >"$out/ui.txt" 2>&1 \
        || { cat "$out/ui.txt"; fail "dofscroll-ui --version exited non-zero: $(head -c 150 "$out/ui.txt" | tr '\n|' '  ')"; }
    echo "dofscroll-ui --version: $(tr '\n' '/' < "$out/ui.txt")"
    qtv="$(sed -n 's/^Qt \([0-9][0-9.]*\) .*/\1/p' "$out/ui.txt")"
    [ -n "$qtv" ] || fail "dofscroll-ui --version has no Qt line"
    [ -z "${EXPECT_QT_VERSION:-}" ] || [ "$qtv" = "$EXPECT_QT_VERSION" ] || fail "dofscroll-ui runs on Qt $qtv, expected $EXPECT_QT_VERSION"
    # 4. Headless self-test of the widgets.
    QT_QPA_PLATFORM=offscreen dofscroll-ui --self-test >"$out/ui-selftest.txt" 2>&1 \
        || { grep -E 'failed|too wide|scroll bar|inspector|does not wrap' "$out/ui-selftest.txt" | head -n 40; tail -n 20 "$out/ui-selftest.txt"; fail "dofscroll-ui --self-test exited non-zero: $(tail -c 150 "$out/ui-selftest.txt" | tr '\n|' '  ')"; }
    echo "dofscroll-ui --self-test: ok"
    # 5. The X11 and Wayland plugins load (without a display the plugin starts and then reports that
    #    it cannot connect; a missing library would say "cannot open shared object file").
    (QT_QPA_PLATFORM=wayland XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=none dofscroll-ui --self-test >"$out/wayland.txt" 2>&1) 2>/dev/null
    grep -q 'wl_display' "$out/wayland.txt" && ! grep -q 'cannot open shared object' "$out/wayland.txt" \
        || { head -n 8 "$out/wayland.txt"; fail "the Wayland platform plugin did not load"; }
    (env -u DISPLAY QT_QPA_PLATFORM=xcb dofscroll-ui --self-test >"$out/xcb.txt" 2>&1) 2>/dev/null
    grep -q -i 'could not connect to display' "$out/xcb.txt" && ! grep -q 'cannot open shared object' "$out/xcb.txt" \
        || { head -n 8 "$out/xcb.txt"; fail "the xcb platform plugin did not load"; }
    echo "platform plugins: xcb and wayland load"
    ui="UI ok, Qt $qtv bundled"
fi
echo "| $image | $kind | installed ok ($ver, $ui) | |" > "$out/summary.md"
