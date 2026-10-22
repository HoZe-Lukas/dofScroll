#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of the two upgrade steps of the Linux packages: packaging/cpack/preinst (RPM %pre / DEB preinst) stops the
# dofscroll user units of every logged-in user before the files are replaced, packaging/cpack/postinst starts
# both again afterwards, whether they were running before or not. Runs both scripts against fake loginctl,
# systemctl, getent and timeout programs; nothing real is touched.
# Usage: sh tests/test_preinst_upgrade.sh <repo dir>
set -u
SRC=${1:?usage: test_preinst_upgrade.sh <repo dir>}
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "test_preinst_upgrade: FAIL: $*" >&2; cat "$T/log" 2>/dev/null >&2; exit 1; }

mkdir -p "$T/bin"
cat > "$T/bin/loginctl" <<EOF
#!/bin/sh
echo "1000 \${FAKE_USER:-alice} 1 no"
EOF
cat > "$T/bin/id" <<EOF
#!/bin/sh
echo 1000
EOF
cat > "$T/bin/pgrep" <<EOF
#!/bin/sh
exit 0
EOF
cat > "$T/bin/sleep" <<EOF
#!/bin/sh
exit 0
EOF
cat > "$T/bin/getent" <<EOF
#!/bin/sh
echo "\$2:x:1000:1000::$T/home/\$2:/bin/sh"
EOF
cat > "$T/bin/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$T/log"
case "\$*" in
    *"cat spacenavd.service"*) exit \${FAKE_SPACENAVD_EXISTS:-0} ;;
    *"is-enabled"*spacenavd*) exit \${FAKE_SPACENAVD_ENABLED:-1} ;;
    *"enable --now spacenavd"*) exit \${FAKE_ENABLE_RC:-0} ;;
    *is-active*) exit \${FAKE_ACTIVE:-0} ;;
    *is-enabled*) exit 1 ;;
esac
exit 0
EOF
cat > "$T/bin/timeout" <<EOF
#!/bin/sh
shift
exec "\$@"
EOF
chmod +x "$T/bin"/loginctl "$T/bin"/id "$T/bin"/pgrep "$T/bin"/sleep "$T/bin"/getent "$T/bin"/systemctl "$T/bin"/timeout
: > "$T/ui.service"
# The scripts under test, with the absolute system paths redirected into the sandbox.
for f in preinst postinst; do
    sed -e "s#/usr/lib/systemd/user/dofscroll-ui.service#$T/ui.service#" \
        -e "s#/proc/#$T/proc/#" -e "s#/etc/systemd/user/#$T/etc/systemd/user/#" \
        -e "s#/run/systemd/system#$T/run-systemd#" "$SRC/packaging/cpack/$f" > "$T/$f"
done
grep -q "$T/ui.service" "$T/postinst" || fail "path substitution did not apply to postinst"
mkdir -p "$T/home/alice" "$T/home/bob@corp.example"
run() { s=$1; shift; PATH="$T/bin:$PATH" sh "$T/$s" "$@" || fail "$s exited non-zero ($*)"; }
reset() { rm -f "$T/log"; : > "$T/log"; }
M="systemctl --user --machine=alice@.host"
line() { grep -n "$1" "$T/log" | head -1 | cut -d: -f1; }

# 1. Upgrade (RPM $1 = 2): the interface is stopped first, then the daemon; after the files are in place the
#    daemon is started first, then the interface.
reset
run preinst 2
grep -q "^$M stop dofscroll-ui.service" "$T/log" || fail "case 1: interface not stopped"
grep -q "^$M stop dofscroll.service" "$T/log" || fail "case 1: daemon not stopped"
[ "$(line "stop dofscroll-ui.service")" -lt "$(line "stop dofscroll.service")" ] || fail "case 1: the interface must be stopped first"
: > "$T/log"
run postinst 2
grep -q "^$M daemon-reload" "$T/log" || fail "case 1: no daemon-reload"
grep -q "^$M start dofscroll.service" "$T/log" || fail "case 1: daemon not started again"
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 1: interface not started again"
[ "$(line "start dofscroll.service")" -lt "$(line "start dofscroll-ui.service")" ] || fail "case 1: the daemon must start first"
[ "$(line "daemon-reload")" -lt "$(line "start dofscroll.service")" ] || fail "case 1: daemon-reload must come before the start"

# 2. Started whether it was running before or not (the units are inactive in the fake).
reset
FAKE_ACTIVE=3 run postinst 2
grep -q "^$M start dofscroll.service" "$T/log" || fail "case 2: daemon not started although it was not running before"
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 2: interface not started although it was not running before"

# 3. Fresh install (RPM $1 = 1, DEB install): nothing at all.
reset
run preinst 1
run preinst install
[ ! -s "$T/log" ] || fail "case 3: systemctl called by preinst on a fresh install"
run postinst 1
run postinst configure ""
[ ! -s "$T/log" ] || fail "case 3: postinst called systemctl on a fresh install"

# 4. DEB upgrade form: preinst upgrade <old version>, postinst configure <old version>.
reset
run preinst upgrade 1.2.1
grep -q "^$M stop dofscroll.service" "$T/log" || fail "case 4: no stop in the DEB upgrade preinst"
: > "$T/log"
run postinst configure 1.2.1
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 4: interface not started after a DEB upgrade"

# 5. A domain account (user@domain) is handled too.
reset
FAKE_USER="bob@corp.example" run preinst 2
grep -q "^systemctl --user --machine=bob@corp.example@.host stop dofscroll.service" "$T/log" || fail "case 5: daemon of the domain user not stopped"
: > "$T/log"
FAKE_USER="bob@corp.example" run postinst 2
grep -q "^systemctl --user --machine=bob@corp.example@.host start dofscroll-ui.service" "$T/log" || fail "case 5: interface of the domain user not started"

# 6. A name that starts with a dash is not used (it would be read as an option).
reset
FAKE_USER="-evil" run preinst 2
FAKE_USER="-evil" run postinst 2
[ ! -s "$T/log" ] || fail "case 6: systemctl called for a name starting with a dash"

# 7. Without timeout nothing is done (every call is time-limited).
reset
for f in preinst postinst; do
    sed 's/command -v timeout >/command -v no-such-timeout-tool >/' "$T/$f" > "$T/${f}7"
    grep -q no-such-timeout-tool "$T/${f}7" || fail "case 7: timeout check not found in $f"
    PATH="$T/bin:$PATH" sh "$T/${f}7" 2 || fail "case 7: $f exited non-zero"
done
[ ! -s "$T/log" ] || fail "case 7: systemctl called although timeout is missing"

# 8. Without a ui unit (a package without the interface) only the daemon is started.
reset
rm -f "$T/ui.service"
run postinst 2
grep -q "^$M start dofscroll.service" "$T/log" || fail "case 8: daemon not started"
grep -q "start dofscroll-ui.service" "$T/log" && fail "case 8: interface started although the package has no interface unit"
: > "$T/ui.service"

# 10. spacenavd (the SpaceMouse driver service): on every install and upgrade, a fresh install included, the
#     system service is enabled and started when its unit exists and is not enabled. A fresh install must not
#     touch the user's dofscroll units (the interface starts the daemon at the first start).
mkdir -p "$T/run-systemd"
SC="systemctl enable --now spacenavd.service"
for args in "1" "configure \"\""; do
    reset
    eval "run postinst $args"
    grep -q "^$SC" "$T/log" || fail "case 10: spacenavd not enabled on a fresh install ($args)"
    grep -q -- "--user\|dofscroll" "$T/log" && fail "case 10: a fresh install touched the user's dofscroll units ($args)"
done
reset
run postinst 2
grep -q "^$SC" "$T/log" || fail "case 10: spacenavd not enabled on an upgrade"
reset
run postinst configure 1.2.1
grep -q "^$SC" "$T/log" || fail "case 10: spacenavd not enabled on a DEB upgrade"
grep -q "^$SC" "$T/log" && [ "$(line "cat spacenavd.service")" -lt "$(line "enable --now spacenavd")" ] || fail "case 10: the unit must be looked up first"

# 11. Already enabled, or no such unit: nothing is enabled.
reset
FAKE_SPACENAVD_ENABLED=0 run postinst 1
grep -q "enable --now spacenavd" "$T/log" && fail "case 11: an enabled spacenavd was enabled again"
reset
FAKE_SPACENAVD_EXISTS=1 run postinst 1
grep -q "enable --now spacenavd" "$T/log" && fail "case 11: spacenavd enabled although its unit does not exist"

# 12. A failing enable never fails the package transaction.
reset
FAKE_ENABLE_RC=1 run postinst 1

# 13. No running systemd (chroot, image build), or a DEB action that is not configure: nothing.
reset
rm -rf "$T/run-systemd"
run postinst 1
run postinst 2
[ "$(grep -c "spacenavd" "$T/log")" = 0 ] || fail "case 13: systemctl called for spacenavd without a running systemd"
mkdir -p "$T/run-systemd"
reset
run postinst abort-upgrade 1.2.1
[ ! -s "$T/log" ] || fail "case 13: abort-upgrade did something"

# 14. Without timeout the spacenavd step is skipped too (every call is time-limited).
reset
sed 's/command -v timeout >/command -v no-such-timeout-tool >/' "$T/postinst" > "$T/postinst14"
PATH="$T/bin:$PATH" sh "$T/postinst14" 1 || fail "case 14: postinst exited non-zero"
[ ! -s "$T/log" ] || fail "case 14: systemctl called although timeout is missing"
rm -rf "$T/run-systemd"

# 9. Neither script touches the configuration of a user, and neither can fail the transaction.
grep -q 'dofscroll\.conf\|\.config/dofscroll' "$SRC/packaging/cpack/preinst" && fail "case 9: preinst touches the configuration"
grep -q '^set -[a-z]*e' "$SRC/packaging/cpack/preinst" "$SRC/packaging/cpack/postinst" && fail "case 9: a script uses set -e"
[ "$(tail -n 1 "$SRC/packaging/cpack/preinst")" = "exit 0" ] || fail "case 9: preinst must end with exit 0"
[ "$(tail -n 1 "$SRC/packaging/cpack/postinst")" = "exit 0" ] || fail "case 9: postinst must end with exit 0"

echo "test_preinst_upgrade: ok"
