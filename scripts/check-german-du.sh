#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# German texts (po/de.po, site/de/) address the reader with the informal "du", never "Sie".
# Fails on the formal possessives (Ihr, Ihre, ...) and on formal verb forms ("Waehlen Sie",
# "Sie koennen"). Third person ("Sie wird verwendet") is not matched on purpose.  # check-english: allow
# A line can opt out with "check-german-du: allow".
# Usage: scripts/check-german-du.sh
set -eu
cd "$(dirname "$0")/.."

pat='\b(Ihr|Ihre|Ihrem|Ihren|Ihrer|Ihres|Ihnen)\b|\b[^ ]+(en|et|t) Sie\b|\bSie (k.{1,2}nnen|k.{1,2}nnten|m.{1,2}ssen|sollten|m.{1,2}chten|haben|sind|werden|wollen|d.{1,2}rfen|sehen|finden|erhalten|brauchen|ben.{1,2}tigen)\b|\b(wenn|dass|ob|bis|damit|w.{1,2}hrend|falls|sobald|solange|nachdem|bevor) Sie\b'  # check-english: allow
hits=$(grep -InP "$pat" po/de.po site/de/*.html 2>/dev/null | grep -v 'check-german-du: allow' || true)
if [ -n "$hits" ]; then
  echo "Formal German address (Sie/Ihr) found:" >&2
  echo "$hits" | cut -c1-160 >&2
  echo "check-german-du: German texts use the informal du (AGENTS.md)." >&2
  exit 1
fi
echo "check-german-du: ok"
