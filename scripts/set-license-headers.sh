#!/usr/bin/env bash
# Sets/updates the SPDX header line in all source files (src/, tests/) based on
# packaging/LICENSE_ID. License: GPL-3.0-or-later (see LICENSE).
#
# Usage:  scripts/set-license-headers.sh [--apply] ["copyright line"]
#   without --apply: only show what would change (dry run).
# Refuses to work while LICENSE_ID is "UNLICENSED" (license not yet decided).
set -euo pipefail
cd "$(dirname "$0")/.."

apply=0
if [ "${1:-}" = "--apply" ]; then apply=1; shift; fi
copyright="${1:-}"

id="$(tr -d '[:space:]' < packaging/LICENSE_ID)"
if [ -z "$id" ] || [ "$id" = "UNLICENSED" ]; then
    echo "LICENSE_ID is '$id': license not yet decided - nothing to do." >&2
    echo "First set packaging/LICENSE_ID to an SPDX identifier (e.g. MIT, Apache-2.0, GPL-3.0-or-later)." >&2
    exit 2
fi

changed=0
while IFS= read -r f; do
    line="// SPDX-License-Identifier: $id"
    if grep -q '^// SPDX-License-Identifier:' "$f"; then
        if grep -qx "$line" "$f"; then continue; fi
        action="update"
    else
        action="add"
    fi
    changed=$((changed + 1))
    echo "$action: $f"
    [ "$apply" -eq 1 ] || continue
    tmp="$(mktemp)"
    {
        echo "$line"
        [ -n "$copyright" ] && ! grep -q '^// SPDX-FileCopyrightText:' "$f" && echo "// SPDX-FileCopyrightText: $copyright"
        grep -v '^// SPDX-License-Identifier:' "$f"
    } > "$tmp"
    cat "$tmp" > "$f"
    rm -f "$tmp"
done < <(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)

if [ "$apply" -eq 1 ]; then
    echo "$changed file(s) changed."
else
    echo "$changed file(s) affected (dry run, --apply to write)."
fi
