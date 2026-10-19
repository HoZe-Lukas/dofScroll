#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Install smoke test, runs INSIDE a fresh distribution container (see linux-smoke.yml):
#   smoke-install.sh rpm|deb <package-dir> <out-dir> <image>
# Installs the packages with the distribution package manager (dependencies come from the
# distribution repositories), then runs `dofscroll --version`, `dofscroll --help` and, if the
# package contains it, `dofscroll-ui --version` headless (QT_QPA_PLATFORM=offscreen, no display).
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
if command -v dofscroll-ui >/dev/null 2>&1; then
    QT_QPA_PLATFORM=offscreen dofscroll-ui --version >"$out/ui.txt" 2>&1 \
        || { cat "$out/ui.txt"; fail "dofscroll-ui --version exited non-zero: $(head -c 150 "$out/ui.txt" | tr '\n|' '  ')"; }
    echo "dofscroll-ui --version: $(cat "$out/ui.txt")"
    ui="UI ok"
fi
echo "| $image | $kind | installed ok ($ver, $ui) | |" > "$out/summary.md"
