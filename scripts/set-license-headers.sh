#!/usr/bin/env bash
# Setzt/aktualisiert die SPDX-Kopfzeile in allen Quelldateien (src/, tests/) anhand von
# packaging/LICENSE_ID. Lizenz: GPL-3.0-or-later (siehe LICENSE).
#
# Aufruf:  scripts/set-license-headers.sh [--apply] ["Copyright-Zeile"]
#   ohne --apply: nur anzeigen, was geaendert wuerde (Trockenlauf).
# Verweigert die Arbeit, solange LICENSE_ID "UNLICENSED" ist (Lizenz noch nicht entschieden).
set -euo pipefail
cd "$(dirname "$0")/.."

apply=0
if [ "${1:-}" = "--apply" ]; then apply=1; shift; fi
copyright="${1:-}"

id="$(tr -d '[:space:]' < packaging/LICENSE_ID)"
if [ -z "$id" ] || [ "$id" = "UNLICENSED" ]; then
    echo "LICENSE_ID ist '$id': Lizenz noch nicht entschieden - nichts zu tun." >&2
    echo "Zuerst packaging/LICENSE_ID auf eine SPDX-Kennung setzen (z.B. MIT, Apache-2.0, GPL-3.0-or-later)." >&2
    exit 2
fi

changed=0
while IFS= read -r f; do
    line="// SPDX-License-Identifier: $id"
    if grep -q '^// SPDX-License-Identifier:' "$f"; then
        if grep -qx "$line" "$f"; then continue; fi
        action="aktualisieren"
    else
        action="ergaenzen"
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
    echo "$changed Datei(en) geaendert."
else
    echo "$changed Datei(en) betroffen (Trockenlauf, --apply zum Schreiben)."
fi
