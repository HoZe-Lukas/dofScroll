#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# German texts (po/de.po, site/de/) address the reader with the informal "du", never "Sie".
# Fails on the formal possessives (Ihr, Ihre, ...) and on formal verb forms ("Wählen Sie",
# "Sie können"). "Sie" as the third person ("Sie wird verwendet") is not matched on purpose.
# A line can opt out with "check-german-du: allow".
# Usage: scripts/check-german-du.sh
set -eu
cd "$(dirname "$0")/.."

pat='\b(Ihr|Ihre|Ihrem|Ihren|Ihrer|Ihres|Ihnen)\b|\b[a-zäöüß]+(en|et|t) Sie\b|\bSie (können|könnten|müssen|sollten|möchten|haben|sind|werden|wollen|dürfen|sehen|finden|erhalten|brauchen|benötigen)\b|\b(wenn|dass|ob|bis|damit|während|falls|sobald|solange|nachdem|bevor) Sie\b'
hits=$(grep -InP "$pat" po/de.po site/de/*.html 2>/dev/null | grep -v 'check-german-du: allow' || true)
if [ -n "$hits" ]; then
  echo "Formal German address (Sie/Ihr) found:" >&2
  echo "$hits" | cut -c1-160 >&2
  echo "check-german-du: German texts use the informal du (AGENTS.md)." >&2
  exit 1
fi
echo "check-german-du: ok"
