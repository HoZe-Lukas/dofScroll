#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Stops the dofscroll LaunchAgent and removes its plist (configuration and logs are kept).
#
#   uninstall-launchagent.sh [--dry-run]
#
# Idempotent: nothing to do is not an error.
set -eu

LABEL="io.github.dofscroll.daemon"
DRY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1; shift ;;
        -h|--help) sed -n '5,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "uninstall-launchagent: unknown option '$1'" >&2; exit 2 ;;
    esac
done

[ -n "${HOME:-}" ] || { echo "uninstall-launchagent: HOME is not set" >&2; exit 1; }
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

if [ "$DRY" -eq 1 ]; then
    echo "# dry run: would run: launchctl bootout $DOMAIN/$LABEL (failure ignored)"
    echo "# dry run: would remove $PLIST"
    exit 0
fi

launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
if [ -e "$PLIST" ]; then
    rm -f "$PLIST"
    echo "removed $PLIST"
else
    echo "no plist at $PLIST"
fi
