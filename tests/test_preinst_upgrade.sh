#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of the two upgrade steps of the Linux packages: packaging/cpack/preinst (RPM %pre / DEB preinst) stops the
# running dofscroll user units before the files are replaced and records them, packaging/cpack/postinst starts
# exactly those again afterwards. Runs both scripts against fake loginctl, systemctl, getent and timeout programs;
# nothing real is touched.
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
# is-active answers per unit: FAKE_DAEMON / FAKE_UI = 0 (active) or 3 (inactive)
cat > "$T/bin/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$T/log"
case "\$*" in
    *is-active*dofscroll-ui.service*) exit \${FAKE_UI:-0} ;;
    *is-active*dofscroll.service*) exit \${FAKE_DAEMON:-0} ;;
    *is-active*) exit 3 ;;
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
    sed -e "s#/run/dofscroll-upgrade.state#$T/state#" -e "s#/usr/lib/systemd/user/dofscroll-ui.service#$T/ui.service#" \
        -e "s#/proc/#$T/proc/#" -e "s#/etc/systemd/user/#$T/etc/systemd/user/#" "$SRC/packaging/cpack/$f" > "$T/$f"
    grep -q "$T/state" "$T/$f" || fail "path substitution did not apply to $f"
done
mkdir -p "$T/home/alice" "$T/home/bob@corp.example"
run() { s=$1; shift; PATH="$T/bin:$PATH" sh "$T/$s" "$@" || fail "$s exited non-zero ($*)"; }
reset() { rm -f "$T/state" "$T/log"; : > "$T/log"; }
M="systemctl --user --machine=alice@.host"
line() { grep -n "$1" "$T/log" | head -1 | cut -d: -f1; }

# 1. Upgrade (RPM $1 = 2), daemon and interface running: stopped (interface first), recorded, started again (daemon first).
reset
run preinst 2
grep -q "^$M stop dofscroll-ui.service" "$T/log" || fail "case 1: interface not stopped"
grep -q "^$M stop dofscroll.service" "$T/log" || fail "case 1: daemon not stopped"
[ "$(line "stop dofscroll-ui.service")" -lt "$(line "stop dofscroll.service")" ] || fail "case 1: the interface must be stopped first"
[ "$(cat "$T/state")" = "alice 1 1" ] || fail "case 1: state file is '$(cat "$T/state")'"
: > "$T/log"
run postinst 2
grep -q "^$M start dofscroll.service" "$T/log" || fail "case 1: daemon not started again"
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 1: interface not started again"
[ "$(line "start dofscroll.service")" -lt "$(line "start dofscroll-ui.service")" ] || fail "case 1: the daemon must start first"
grep -q 'try-restart' "$T/log" && fail "case 1: try-restart although the units were recorded"
[ ! -e "$T/state" ] || fail "case 1: state file not removed"

# 2. Only the daemon was running: only the daemon is started again.
reset
FAKE_UI=3 run preinst 2
[ "$(cat "$T/state")" = "alice 1 0" ] || fail "case 2: state file is '$(cat "$T/state")'"
grep -q "stop dofscroll-ui.service" "$T/log" && fail "case 2: stopped an interface that was not running"
: > "$T/log"
FAKE_UI=3 run postinst 2
grep -q "^$M start dofscroll.service" "$T/log" || fail "case 2: daemon not started"
grep -q "start dofscroll-ui.service" "$T/log" && fail "case 2: started an interface that was not running before"

# 3. Nothing was running: nothing stopped, nothing started by the recorded path; the old try-restart remains.
reset
FAKE_DAEMON=3 FAKE_UI=3 run preinst 2
grep -q " stop " "$T/log" && fail "case 3: stopped something that was not running"
[ ! -s "$T/state" ] || fail "case 3: state file not empty"
: > "$T/log"
FAKE_DAEMON=3 FAKE_UI=3 run postinst 2
grep -q "^$M try-restart dofscroll.service dofscroll-ui.service" "$T/log" || fail "case 3: no try-restart fallback"
grep -q "^$M start dofscroll" "$T/log" && fail "case 3: started a unit that was not running before"

# 4. Fresh install (RPM $1 = 1, DEB install): nothing at all.
reset
run preinst 1
run preinst install
[ ! -s "$T/log" ] || fail "case 4: systemctl called on a fresh install"
[ ! -e "$T/state" ] || fail "case 4: state file written on a fresh install"
run postinst 1
run postinst configure ""
[ ! -s "$T/log" ] || fail "case 4: postinst called systemctl on a fresh install"

# 5. DEB upgrade form: preinst upgrade <old version>, postinst configure <old version>.
reset
run preinst upgrade 1.2.1
[ "$(cat "$T/state")" = "alice 1 1" ] || fail "case 5: no state after a DEB upgrade preinst"
: > "$T/log"
run postinst configure 1.2.1
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 5: interface not started after a DEB upgrade"

# 6. A domain account (user@domain) is handled too.
reset
FAKE_USER="bob@corp.example" run preinst 2
[ "$(cat "$T/state")" = "bob@corp.example 1 1" ] || fail "case 6: state file is '$(cat "$T/state")'"
: > "$T/log"
FAKE_USER="bob@corp.example" run postinst 2
grep -q "^systemctl --user --machine=bob@corp.example@.host start dofscroll-ui.service" "$T/log" || fail "case 6: interface of the domain user not started"

# 7. A name that starts with a dash is not used (it would be read as an option).
reset
FAKE_USER="-evil" run preinst 2
grep -q -e "--machine=-evil" "$T/log" && fail "case 7: systemctl called for a name starting with a dash"
: > "$T/log"
FAKE_USER="-evil" run postinst 2
[ ! -s "$T/log" ] || fail "case 7: postinst called systemctl for a name starting with a dash"

# 8. Without timeout nothing is done (every call is time-limited).
reset
sed 's/command -v timeout >/command -v no-such-timeout-tool >/' "$T/preinst" > "$T/preinst8"
grep -q no-such-timeout-tool "$T/preinst8" || fail "case 8: timeout check not found"
PATH="$T/bin:$PATH" sh "$T/preinst8" 2 || fail "case 8: preinst exited non-zero"
[ ! -s "$T/log" ] || fail "case 8: systemctl called although timeout is missing"

# 9. A symbolic link in place of the state file is not read.
reset
ln -s "$T/elsewhere" "$T/state"
printf 'alice 1 1\n' > "$T/elsewhere"
run postinst 2
grep -q "^$M start dofscroll" "$T/log" && fail "case 9: followed a symbolic link as state file"
rm -f "$T/state" "$T/elsewhere"

# 10. A hostile state line (not 0/1) is ignored.
reset
printf 'alice x; y\n' > "$T/state"
run postinst 2
grep -q "^$M start dofscroll" "$T/log" && fail "case 10: started units from a malformed state line"

# 11. The scripts never touch the configuration of a user.
grep -q 'dofscroll\.conf\|\.config/dofscroll' "$SRC/packaging/cpack/preinst" && fail "case 11: preinst touches the configuration"

echo "test_preinst_upgrade: ok"
