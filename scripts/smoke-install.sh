#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Install smoke test, runs INSIDE a fresh distribution container (see linux-smoke.yml):
#   smoke-install.sh rpm|deb <package-dir> <out-dir> <image>
# Installs the packages with the distribution package manager (dependencies come from the
# distribution repositories), then runs `dofscroll --version`, `dofscroll --help` and, if the
# package contains it, `dofscroll-ui --version` headless (QT_QPA_PLATFORM=offscreen, no display).
# The RPM is built against Qt 6.6 (the oldest supported) and installed here with the NEWEST Qt of
# EPEL: the run proves the forward compatibility; the installed Qt version is printed.
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
*) fail "unknown kind $kind" ;;
esac

if [ "$rc" -ne 0 ]; then
    echo "--- install log (tail) ---"; tail -n 30 "$log"
    # First line that names the problem (dnf: "nothing provides", apt: "Depends:")
    err="$(grep -i -m1 -E 'nothing provides|no match for argument|unmet dependencies|depends:|unable to locate|conflicts|error' "$log" | tr '|' '/' | cut -c1-200)"
    fail "install: ${err:-see log}"
fi
echo "installed ok"

command -v dofscroll >/dev/null 2>&1 || fail "dofscroll not on PATH after install"
ver="$(dofscroll --version 2>&1)" || fail "dofscroll --version exited non-zero: $ver"
echo "dofscroll --version: $ver"
dofscroll --help >/dev/null 2>&1 || fail "dofscroll --help exited non-zero"

ui="no UI"
qt=""
if command -v dofscroll-ui >/dev/null 2>&1; then
    case "$kind" in
    rpm) qt="$(rpm -q qt6-qtbase 2>/dev/null || echo 'qt6-qtbase: not installed')" ;;
    deb) qt="$(dpkg-query -W -f='${Package} ${Version}' libqt6core6t64 2>/dev/null || dpkg-query -W -f='${Package} ${Version}' libqt6core6 2>/dev/null || echo 'libqt6core6: not installed')" ;;
    esac
    echo "installed Qt: $qt"
    QT_QPA_PLATFORM=offscreen dofscroll-ui --version >"$out/ui.txt" 2>&1 \
        || { cat "$out/ui.txt"; fail "dofscroll-ui --version exited non-zero: $(head -c 150 "$out/ui.txt" | tr '\n|' '  ')"; }
    echo "dofscroll-ui --version: $(cat "$out/ui.txt")"
    # The interface must start with the Qt of this distribution (headless self-test of the widgets)
    QT_QPA_PLATFORM=offscreen dofscroll-ui --self-test >"$out/ui-selftest.txt" 2>&1 \
        || { tail -n 20 "$out/ui-selftest.txt"; fail "dofscroll-ui --self-test exited non-zero: $(tail -c 150 "$out/ui-selftest.txt" | tr '\n|' '  ')"; }
    echo "dofscroll-ui --self-test: ok"
    ui="UI ok, $qt"
fi
echo "| $image | $kind | installed ok ($ver, $ui) | |" > "$out/summary.md"
