#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Installs the dofscroll daemon as a per-user LaunchAgent (no sudo, nothing outside $HOME).
#
#   install-launchagent.sh [--daemon /absolute/path/to/dofscroll] [--dry-run] [--no-load]
#
#   --daemon PATH  daemon binary (default: ../../build/dofscroll next to this script)
#   --dry-run      print the rendered plist and the actions, change nothing
#   --no-load      write the plist but do not run launchctl
#
# Idempotent: running it again replaces the plist and reloads the agent.
set -eu

LABEL="io.github.dofscroll.daemon"
DRY=0
LOAD=1
DAEMON=""

usage() { sed -n '5,13p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --daemon)
            [ $# -ge 2 ] || { echo "install-launchagent: --daemon needs a path" >&2; exit 2; }
            DAEMON="$2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        --no-load) LOAD=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install-launchagent: unknown option '$1'" >&2; exit 2 ;;
    esac
done

HERE=$(cd "$(dirname "$0")" && pwd)
TEMPLATE="$HERE/$LABEL.plist.in"
[ -f "$TEMPLATE" ] || { echo "install-launchagent: template $TEMPLATE not found" >&2; exit 1; }

[ -n "${HOME:-}" ] || { echo "install-launchagent: HOME is not set" >&2; exit 1; }
if [ -z "$DAEMON" ]; then
    DAEMON="$HERE/../../build/dofscroll"
fi
# Resolve to an absolute path (the file must exist unless this is a dry run).
if [ -e "$DAEMON" ]; then
    DAEMON="$(cd "$(dirname "$DAEMON")" && pwd)/$(basename "$DAEMON")"
elif [ "$DRY" -eq 0 ]; then
    echo "install-launchagent: daemon binary '$DAEMON' not found (build it first or pass --daemon)" >&2
    exit 1
fi
case "$DAEMON" in
    /*) ;;
    *) echo "install-launchagent: daemon path must be absolute: $DAEMON" >&2; exit 1 ;;
esac

LOG_DIR="$HOME/Library/Logs/dofscroll"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"

# The paths end up in an XML file and in a sed expression: refuse characters that would need
# escaping instead of trying to escape them.
for v in "$DAEMON" "$LOG_DIR"; do
    case "$v" in
        *'|'* | *'&'* | *'\'* | *'<'* | *'>'* | *'"'* | *"'"*)
            echo "install-launchagent: unsupported character in path: $v" >&2
            exit 1 ;;
    esac
    if [ "$(printf '%s' "$v" | wc -l)" -ne 0 ]; then
        echo "install-launchagent: line break in path" >&2
        exit 1
    fi
done

render() {
    sed -e "s|@DAEMON_PATH@|$DAEMON|g" -e "s|@LOG_DIR@|$LOG_DIR|g" "$TEMPLATE"
}

DOMAIN="gui/$(id -u)"

if [ "$DRY" -eq 1 ]; then
    echo "# dry run: would write $PLIST" >&2
    render
    if [ "$LOAD" -eq 1 ]; then
        echo "# dry run: would run: launchctl bootout $DOMAIN/$LABEL (failure ignored)" >&2
        echo "# dry run: would run: launchctl bootstrap $DOMAIN $PLIST" >&2
    fi
    exit 0
fi

mkdir -p "$AGENT_DIR" "$LOG_DIR"
chmod 700 "$LOG_DIR"
TMP=$(mktemp "$AGENT_DIR/.$LABEL.XXXXXX")
trap 'rm -f "$TMP"' EXIT
render > "$TMP"
chmod 644 "$TMP"   # launchd refuses plists that are group- or world-writable
mv -f "$TMP" "$PLIST"
trap - EXIT
echo "wrote $PLIST"

if [ "$LOAD" -eq 1 ]; then
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "loaded $LABEL in $DOMAIN"
fi
