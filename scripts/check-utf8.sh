#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Fails when a tracked text file is not valid UTF-8 or starts with a byte order mark
# (docs/encoding.md). Tracked file names must be valid UTF-8 too.
# Usage: scripts/check-utf8.sh   (run from anywhere in the repo; needs git and iconv)
#
# Exceptions: *.ps1 may start with a BOM (Windows PowerShell 5.1 reads BOM-less files as ANSI).
# Binary files are skipped: a known binary extension, or a NUL byte in the file.
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"

command -v iconv >/dev/null 2>&1 || { echo "check-utf8: iconv not found" >&2; exit 2; }

fail=0

# File names: the whole list must convert cleanly.
if ! git -c core.quotepath=off ls-files | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1; then
  echo "check-utf8: a tracked file name is not valid UTF-8" >&2
  fail=1
fi

git -c core.quotepath=off ls-files > "${TMPDIR:-/tmp}/check-utf8.$$"
trap 'rm -f "${TMPDIR:-/tmp}/check-utf8.$$"' EXIT INT TERM

while IFS= read -r f; do
  [ -f "$f" ] || continue
  [ -L "$f" ] && continue
  case "$f" in
    *.png|*.jpg|*.jpeg|*.gif|*.ico|*.icns|*.bmp|*.webp|*.pdf|*.zip|*.gz|*.xz|*.bz2|*.tar|*.7z|\
    *.ttf|*.otf|*.woff|*.woff2|*.mo|*.exe|*.dll|*.so|*.a|*.o|*.bin|*.dat) continue ;;
  esac
  # A NUL byte means binary: removing NULs changes the size.
  size=$(wc -c < "$f")
  nonul=$(tr -d '\000' < "$f" | wc -c)
  [ "$size" -eq "$nonul" ] || continue
  if ! iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1; then
    echo "not valid UTF-8: $f" >&2
    fail=1
    continue
  fi
  case "$f" in
    *.ps1) continue ;;
  esac
  first3=$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')
  if [ "$first3" = "efbbbf" ]; then
    echo "starts with a byte order mark (BOM): $f" >&2
    fail=1
  fi
done < "${TMPDIR:-/tmp}/check-utf8.$$"

if [ "$fail" -ne 0 ]; then
  echo "check-utf8: failed (see docs/encoding.md)" >&2
  exit 1
fi
echo "check-utf8: ok"
