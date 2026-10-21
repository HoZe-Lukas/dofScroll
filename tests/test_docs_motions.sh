#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Fails when the six-motion table of the built-in joystick modes in docs/wiki/Built-in-modes.md and
# docs/modes.md no longer matches the mode definitions: V_ACTION / H_ACTION / TWIST_ACTION and the
# *_SENS overrides of examples/modes/*.conf, and the code-defined modes normal, off and timeline
# (src/modes.cpp, repeated below). Row format: | `id` | vpush | vtilt | hpush | htilt | twist | lift | overrides |
# Cell words: default -> Scroll / Scroll sideways / Zoom (by channel), none -> Off, wheel_v -> Scroll,
# wheel_h -> Scroll sideways, zoom -> Zoom, keys:a,b -> "Keys: A / B" (key names capitalized, chords
# joined with +; pageup -> Page Up, pagedown -> Page Down).
#
# Usage: tests/test_docs_motions.sh [repository root]
set -eu

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
fail=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

# cell <channel v|h|t> <action>  ->  the table words
cell() {
  awk -v ch="$1" -v a="$2" 'BEGIN {
    if (a == "default") { print (ch == "v" ? "Scroll" : ch == "h" ? "Scroll sideways" : "Zoom"); exit }
    if (a == "none") { print "Off"; exit }
    if (a == "wheel_v") { print "Scroll"; exit }
    if (a == "wheel_h") { print "Scroll sideways"; exit }
    if (a == "zoom") { print "Zoom"; exit }
    if (a ~ /^(keys|step):/) {
      kind = (a ~ /^keys:/) ? "Keys: " : "Steps: "
      sub(/^[a-z]+:/, "", a)
      split(a, k, ",")
      printf "%s%s / %s\n", kind, chord(k[1]), chord(k[2]); exit
    }
    print "?" a
  }
  function key(s) {
    if (s == "pageup") return "Page Up"
    if (s == "pagedown") return "Page Down"
    return toupper(substr(s, 1, 1)) substr(s, 2)
  }
  function chord(s,   n, p, i, r) {
    n = split(s, p, "+"); r = ""
    for (i = 1; i <= n; i++) r = r (i > 1 ? "+" : "") key(p[i])
    return r
  }'
}

# conf value of KEY in a mode file (empty when absent); strips comments and blanks
confval() {
  sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*\([^#;]*[^#;[:space:]]\).*/\1/p" "$1" | head -n 1
}

# row <page> <id>  ->  the table row of the mode, cells separated by "|"
row() {
  grep "^| \`$2\` |" "$1" | head -n 1 | sed 's/^| *`[^`]*` *| *//; s/ *|$//; s/ *| */|/g'
}

# check <page> <id> <v> <h> <twist> <overrides>
check() {
  page=$1; id=$2
  cv=$(cell v "$3"); ch=$(cell h "$4"); ct=$(cell t "$5")
  expect="$cv|$cv|$ch|$ch|$ct|Not used"
  got=$(row "$page" "$id")
  if [ -z "$got" ]; then
    echo "test_docs_motions: $page has no table row for '$id'" >&2; fail=1; return
  fi
  head6=$(printf '%s' "$got" | cut -d'|' -f1-6)
  if [ "$head6" != "$expect" ]; then
    echo "test_docs_motions: $page row '$id' is stale" >&2
    echo "  table:    $head6" >&2
    echo "  expected: $expect" >&2
    fail=1
  fi
  last=$(printf '%s' "$got" | cut -d'|' -f7-)
  if [ -n "$6" ]; then
    printf '%s\n' "$6" | while IFS= read -r kv; do
      case "$last" in *"$kv"*) ;; *) echo "test_docs_motions: $page row '$id' lacks override '$kv'" >&2; echo bad > "$tmp" ;; esac
    done
    [ ! -s "$tmp" ] || { fail=1; : > "$tmp"; }
  else
    [ "$last" = "none" ] || { echo "test_docs_motions: $page row '$id' lists overrides ('$last') but the mode has none" >&2; fail=1; }
  fi
}

for page in "$root/docs/wiki/Built-in-modes.md" "$root/docs/modes.md"; do
  [ -f "$page" ] || { echo "test_docs_motions: $page is missing" >&2; fail=1; continue; }

  # code-defined modes (src/modes.cpp builtinModes)
  check "$page" normal default default default ""
  check "$page" off none none none ""
  check "$page" timeline none keys:left,right keys:left,right ""

  for f in "$root"/examples/modes/*.conf; do
    id=$(basename "$f" .conf)
    v=$(confval "$f" V_ACTION); h=$(confval "$f" H_ACTION); t=$(confval "$f" TWIST_ACTION)
    ov=""
    for k in SCROLL_V_SENS SCROLL_H_SENS ZOOM_SENS; do
      x=$(confval "$f" "$k")
      [ -z "$x" ] || ov="$ov$k = $x
"
    done
    check "$page" "$id" "${v:-default}" "${h:-default}" "${t:-default}" "$(printf '%s' "$ov" | sed '/^$/d')"
  done
done

# the code-defined modes must still be defined as assumed above
grep -q 'tl.v.type = ChannelActionType::Off' "$root/src/modes.cpp" || { echo "test_docs_motions: timeline changed in src/modes.cpp; update this test and the tables" >&2; fail=1; }
grep -q 'tl.h.neg = tl.twist.neg = Key::Left' "$root/src/modes.cpp" || { echo "test_docs_motions: timeline keys changed in src/modes.cpp; update this test and the tables" >&2; fail=1; }

[ "$fail" -eq 0 ] || exit 1
echo "test_docs_motions: the six-motion tables match the mode definitions"
