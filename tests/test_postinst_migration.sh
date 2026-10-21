#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of packaging/cpack/postinst (RPM %post / DEB postinst): the upgrade step moves a user of the
# removed GTK tray unit to dofscroll-ui.service. Runs the script against fake loginctl, systemctl,
# getent and timeout programs and a fake home directory; nothing real is touched.
# Usage: sh tests/test_postinst_migration.sh <repo dir>
set -u
SRC=${1:?usage: test_postinst_migration.sh <repo dir>}
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "test_postinst_migration: FAIL: $*" >&2; cat "$T/log" 2>/dev/null >&2; exit 1; }

mkdir -p "$T/bin" "$T/etc/systemd/user"
cat > "$T/bin/loginctl" <<EOF
#!/bin/sh
echo "1000 \${FAKE_USER:-alice} 1 no"
EOF
# the uid lookup, the process list, sleep and kill of the script (nothing real is touched)
cat > "$T/bin/id" <<EOF
#!/bin/sh
echo 1000
EOF
cat > "$T/bin/pgrep" <<EOF
#!/bin/sh
[ -n "\${FAKE_UI_PID:-}" ] && echo "\$FAKE_UI_PID"
exit 0
EOF
cat > "$T/bin/sleep" <<EOF
#!/bin/sh
exit 0
EOF
cat > "$T/bin/kill" <<EOF
#!/bin/sh
echo "kill \$*" >> "$T/log"
exit 0
EOF
cat > "$T/bin/getent" <<EOF
#!/bin/sh
echo "\$2:x:1000:1000::$T/home/\$2:/bin/sh"
EOF
cat > "$T/bin/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$T/log"
case "\$*" in *is-active*) exit \${FAKE_ACTIVE:-0} ;; esac
exit 0
EOF
cat > "$T/bin/timeout" <<EOF
#!/bin/sh
shift
exec "\$@"
EOF
chmod +x "$T/bin"/*
# The script under test, with the two absolute system paths redirected into the sandbox.
sed -e "s#/usr/lib/systemd/user/dofscroll-ui.service#$T/ui.service#" \
    -e "s#/proc/#$T/proc/#" -e "s#kill -TERM#$T/bin/kill -TERM#" \
    -e "s#/etc/systemd/user/#$T/etc/systemd/user/#" "$SRC/packaging/cpack/postinst" > "$T/postinst"
grep -q "$T/ui.service" "$T/postinst" || fail "path substitution did not apply"

setup() {  # $1 = with tray link (1/0), $2 = ui unit present (1/0)
    rm -rf "$T/home" "$T/log" "$T/ui.service"
    mkdir -p "$T/home/alice/.config/dofscroll" "$T/home/alice/.config/systemd/user/default.target.wants"
    echo "SCROLL_V_SENS = 7" > "$T/home/alice/.config/dofscroll/dofscroll.conf"
    if [ "$1" = 1 ]; then
        ln -s "$T/gone/dofscroll-tray.service" \
            "$T/home/alice/.config/systemd/user/default.target.wants/dofscroll-tray.service"
    fi
    if [ "$2" = 1 ]; then : > "$T/ui.service"; fi
    : > "$T/log"
}
run() { PATH="$T/bin:$PATH" sh "$T/postinst" "$@" || fail "postinst exited non-zero ($*)"; }
link="$T/home/alice/.config/systemd/user/default.target.wants/dofscroll-tray.service"
M="systemctl --user --machine=alice@.host"

# 1. Upgrade (RPM $1 = 2), tray enabled and running: migrated.
setup 1 1
FAKE_ACTIVE=0 run 2
[ ! -L "$link" ] || fail "dangling tray link was not removed"
grep -q "^$M stop dofscroll-tray.service" "$T/log" || fail "tray not stopped"
grep -q "^$M disable dofscroll-tray.service" "$T/log" || fail "tray not disabled"
grep -q "^$M enable dofscroll-ui.service" "$T/log" || fail "ui not enabled"
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "ui not started although the tray was running"
grep -q "^$M try-restart dofscroll.service dofscroll-ui.service" "$T/log" || fail "try-restart list wrong"
grep -q 'try-restart.*dofscroll-tray' "$T/log" && fail "removed tray unit still in try-restart"
[ "$(cat "$T/home/alice/.config/dofscroll/dofscroll.conf")" = "SCROLL_V_SENS = 7" ] || fail "config changed"

# 2. Tray enabled but not running: enabled for the next login, not started now.
setup 1 1
FAKE_ACTIVE=3 run 2
grep -q "^$M enable dofscroll-ui.service" "$T/log" || fail "case 2: ui not enabled"
grep -q "^$M start dofscroll-ui.service" "$T/log" && fail "case 2: ui started although the tray was not running"

# 3. DEB upgrade form (configure <old version>) migrates too.
setup 1 1
run configure 0.1.0~beta.2
grep -q "^$M enable dofscroll-ui.service" "$T/log" || fail "case 3: no migration on DEB configure"

# 4. Fresh install (RPM $1 = 1, DEB without old version): nothing at all.
setup 1 1
run 1
[ -L "$link" ] || fail "case 4: link touched on a fresh install"
[ ! -s "$T/log" ] || fail "case 4: systemctl called on a fresh install"
setup 1 1
run configure ""
[ ! -s "$T/log" ] || fail "case 4b: systemctl called on a fresh DEB install"

# 5. User never used the tray: no enable of the UI unit.
setup 0 1
run 2
grep -q "enable dofscroll-ui.service" "$T/log" && fail "case 5: ui enabled for a user without the tray"

# 6. Package without the Qt UI unit: the old link stays, nothing is enabled.
setup 1 0
run 2
[ -L "$link" ] || fail "case 6: link removed although there is no UI to replace the tray"
grep -q "enable dofscroll-ui.service" "$T/log" && fail "case 6: ui enabled without a ui unit"

# 7. A domain account (user@domain) is restarted too.
setup 0 1
FAKE_USER="bob@corp.example" run 2
grep -q "^systemctl --user --machine=bob@corp.example@.host try-restart dofscroll.service dofscroll-ui.service" "$T/log" || fail "case 7: no restart for a user name with @"

# 8. An interface that the unit did not start and that runs a replaced binary is stopped and started via the unit.
setup 0 1
mkdir -p "$T/proc/4242"
ln -s "/usr/bin/dofscroll-ui (deleted)" "$T/proc/4242/exe"
FAKE_ACTIVE=3 FAKE_UI_PID=4242 run 2
grep -q "^kill -TERM 4242" "$T/log" || fail "case 8: stale interface not stopped"
grep -q "^$M start dofscroll-ui.service" "$T/log" || fail "case 8: unit not started after stopping the stale interface"

# 9. The same process with an intact binary is left alone.
setup 0 1
mkdir -p "$T/proc/4243"
ln -s "/usr/bin/dofscroll-ui" "$T/proc/4243/exe"
FAKE_ACTIVE=3 FAKE_UI_PID=4243 run 2
grep -q "^kill" "$T/log" && fail "case 9: a current interface was stopped"

# 10. The unit is active (try-restart did its job): nothing is killed.
setup 0 1
mkdir -p "$T/proc/4244"
ln -s "/usr/bin/dofscroll-ui (deleted)" "$T/proc/4244/exe"
FAKE_ACTIVE=0 FAKE_UI_PID=4244 run 2
grep -q "^kill" "$T/log" && fail "case 10: killed although the unit is active"

echo "test_postinst_migration: ok"
