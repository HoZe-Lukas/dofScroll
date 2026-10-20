#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Runs scripts/test-driver-coexistence.sh non-interactively with a fake hidprobe and checks the
# report: the verdict lines, the scrubbing of the home folder and the user name, the exit code.
# usage: test_coexistence_script.sh <source dir>
set -eu
trap 'echo "test failed at line $LINENO" >&2' ERR
SRC=${1:?source dir}
T=$(mktemp -d "${TMPDIR:-/tmp}/dofscroll-coexist-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/home/someuser" "$T/work"

# fake hidprobe: one device, --check receives 12 reports and prints the RESULT line
cat > "$T/bin/dofscroll-hidprobe" <<'EOF'
#!/bin/sh
case "$1" in
    --version) echo "dofscroll-hidprobe 0.0.0-test" ;;
    --list) echo "fake-device   256f:c635  3Dconnexion SpaceMouse Compact  [matches \"SpaceMouse Compact\"]  $HOME/secret" ;;
    --check) printf 'tx=   0.0 \r'; echo "RESULT opened=yes reports=12 axes_moved=tx,ty buttons_seen=1"; exit 0 ;;
esac
EOF
chmod +x "$T/bin/dofscroll-hidprobe"

cd "$T/work"
HOME="$T/home/someuser" USER=someuser bash "$SRC/scripts/test-driver-coexistence.sh" --probe "$T/bin/dofscroll-hidprobe" \
    --non-interactive > out.txt
R=dofscroll-driver-test-report.txt
test -s "$R"
grep -q '^A: device opened while the driver runs: YES' "$R"
grep -q '^A: reports received while the driver runs: YES (12 reports)' "$R"
grep -q '^A: axes seen: tx,ty' "$R"
grep -q '^Driver running: ' "$R"
grep -q '^B: skipped' "$R"
grep -q '^C: skipped' "$R"
! grep -q "$T/home" "$R"
! grep -q 'someuser' "$R"
grep -q '/secret' "$R"
# shellcheck disable=SC2088  # a literal tilde is what the report must contain
grep -q '~/secret' "$R"
grep -q 'issue #37' out.txt

# a probe that cannot open the device: the error text is kept, the verdicts say NO
cat > "$T/bin/dofscroll-hidprobe" <<'EOF'
#!/bin/sh
case "$1" in
    --version) echo "dofscroll-hidprobe 0.0.0-test" ;;
    --list) echo "fake-device   256f:c635  3Dconnexion SpaceMouse Compact  [matches \"SpaceMouse Compact\"]" ;;
    --check) echo 'RESULT opened=no error="IOHIDDeviceOpen: held exclusively (IOReturn 0xE00002C5)"'; exit 1 ;;
esac
EOF
HOME="$T/home/someuser" USER=someuser bash "$SRC/scripts/test-driver-coexistence.sh" --probe "$T/bin/dofscroll-hidprobe" \
    --non-interactive --report "$T/work/second.txt" > /dev/null
grep -q '^A: device opened while the driver runs: NO' second.txt
grep -q '^A: reports received while the driver runs: NO' second.txt
grep -q 'error text: IOHIDDeviceOpen: held exclusively' second.txt

# bad usage
if bash "$SRC/scripts/test-driver-coexistence.sh" --bogus > /dev/null 2>&1; then echo "bad option accepted"; exit 1; fi
echo "coexistence script test passed"
