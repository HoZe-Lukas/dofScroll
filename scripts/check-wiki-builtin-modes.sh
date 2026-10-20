#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Fails when a built-in mode is missing from docs/wiki/Built-in-modes.md, so the wiki page cannot
# go stale. Checked: the code-defined joystick modes (normal, off, timeline) and the id of every
# file in examples/modes/ and examples/buttons/ (these are embedded in the program at build time).
# An id counts as documented when the page has a heading "### `<id>`" for it.
#
# Usage: scripts/check-wiki-builtin-modes.sh   (run from anywhere; it finds the repository root)
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
page="$root/docs/wiki/Built-in-modes.md"
[ -f "$page" ] || { echo "check-wiki-builtin-modes: $page does not exist" >&2; exit 1; }

fail=0
check() {
  if ! grep -q "^### \`$1\`" "$page"; then
    echo "check-wiki-builtin-modes: $2 '$1' has no heading \"### \`$1\`\" in docs/wiki/Built-in-modes.md" >&2
    fail=1
  fi
}

for id in normal off timeline; do
  check "$id" "code-defined mode"
done
for f in "$root"/examples/modes/*.conf; do
  [ -f "$f" ] || continue
  check "$(basename "$f" .conf)" "joystick mode"
done
for f in "$root"/examples/buttons/*.conf; do
  [ -f "$f" ] || continue
  check "$(basename "$f" .conf)" "button mode"
done

[ "$fail" -eq 0 ] || exit 1
echo "check-wiki-builtin-modes: all built-in modes are on the wiki page"
