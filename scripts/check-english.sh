#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Everything in the repository is English; only the UI translations in po/*.po may be German.
# This check fails on German umlauts/sharp s and on a list of unmistakably German words in all
# tracked text files outside po/ and LICENSE. A line can opt out with "check-english: allow".
# Usage: scripts/check-english.sh
set -eu
cd "$(dirname "$0")/.."

words='nicht|werden|wird|oder|eine|einen|einer|einem|damit|datei|konfiguration|geraet|muss|keine|wenn|sonst|auch|noch|nur|zum|zur|dass|beim|bereits|fehler|ausgabe|eingabe|fuer|ueber|taste|achse|aenderung|zeile|schluessel|gueltig|ungueltig|beenden|starten|oeffnen|speichern|einstellung|einstellungen'

files=$(git ls-files | grep -vE '^(po/|LICENSE$|scripts/check-english\.sh$)' || true)
fail=0
# 1) umlauts and sharp s (the typographic dashes and arrows used in docs are not matched)
hits=$(printf '%s\n' "$files" | xargs grep -InP '[\x{00C4}\x{00D6}\x{00DC}\x{00E4}\x{00F6}\x{00FC}\x{00DF}]' 2>/dev/null | grep -v 'check-english: allow' || true)
if [ -n "$hits" ]; then echo "German characters outside po/:" >&2; echo "$hits" | cut -c1-160 >&2; fail=1; fi
# 2) German words (whole words, case-insensitive)
hits=$(printf '%s\n' "$files" | xargs grep -IniwE "$words" 2>/dev/null | grep -v 'check-english: allow' || true)
if [ -n "$hits" ]; then echo "German words outside po/:" >&2; echo "$hits" | cut -c1-160 >&2; fail=1; fi
if [ "$fail" -ne 0 ]; then
  echo "check-english: the repository is English-only (translations belong in po/*.po); rewrite the text or add 'check-english: allow' to a line that must stay." >&2
  exit 1
fi
echo "check-english: ok"
