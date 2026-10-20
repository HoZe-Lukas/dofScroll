#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Fails when a file under DIR (an unpacked package) contains a CI runner, build or staging path.
# Binary files are scanned too (a PDB path or an rpath would show up there).
#
# Usage: scripts/scan-package-paths.sh DIR PATTERN...
#   Each PATTERN is a fixed string (e.g. the workspace path in both slash forms).
set -eu

[ $# -ge 2 ] || { echo "usage: $0 DIR PATTERN..." >&2; exit 2; }
dir=$1
shift
[ -d "$dir" ] || { echo "scan-package-paths: not a directory: $dir" >&2; exit 2; }

rc=0
n=0
for p in "$@"; do
    [ -n "$p" ] || continue
    n=$((n + 1))
    if grep -rlaiF -e "$p" "$dir"; then
        echo "scan-package-paths: package contains the path '$p'" >&2
        rc=1
    fi
done
[ "$n" -gt 0 ] || { echo "scan-package-paths: no pattern" >&2; exit 2; }
[ "$rc" -eq 0 ] && echo "scan-package-paths: ok ($n patterns)"
exit "$rc"
