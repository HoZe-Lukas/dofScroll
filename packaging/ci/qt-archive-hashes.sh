#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Own SHA-256 of every Qt archive that aqtinstall downloads (issue #265). aqtinstall checks an archive against a
# hash that it fetches from the same server (download.qt.io); packaging/ci/qt-archives.sha256 holds a hash that
# only a reviewed commit can change, so a changed or replaced archive is noticed. The archives are kept with
# `aqt install-qt ... --keep --archive-dest <dir>` and checked here before the build goes on.
#
#   qt-archive-hashes.sh verify   <archive dir>   exit 1 if an archive is not in the manifest or its hash differs
#   qt-archive-hashes.sh generate <archive dir>   print manifest lines ("<sha256>  <file name>") for the archives
#
# The manifest lines of one Qt version start with its file name "<QT_VERSION>-0-<build>...". While the manifest has no
# line for the pinned QT_VERSION (nothing recorded yet), `verify` only warns and exits 0, unless
# QT_ARCHIVES_REQUIRED=1 is set; once lines exist, every downloaded archive MUST be listed with the same hash.
# Record the hashes by starting the workflow qt-archive-hashes (Actions tab) and committing its output
# (docs/release-process.md, "Qt archive hashes"). POSIX sh; runs on Linux, macOS and Windows (Git Bash).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
manifest="${QT_ARCHIVES_MANIFEST:-$here/qt-archives.sha256}"
mode="${1:-}"
dir="${2:-}"
[ -n "$mode" ] && [ -n "$dir" ] || { echo "usage: qt-archive-hashes.sh verify|generate <archive dir>" >&2; exit 2; }
[ -d "$dir" ] || { echo "qt-archive-hashes: $dir is not a directory" >&2; exit 2; }
if [ -z "${QT_VERSION:-}" ]; then
  # shellcheck disable=SC1091
  . "$here/../qt.env"
fi

sha() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else echo "qt-archive-hashes: sha256sum or shasum is needed" >&2; exit 2; fi
}

# "<sha256>  <file name>" for every archive below $dir, sorted by name
lines() {
  find "$dir" -type f | while IFS= read -r f; do
    printf '%s  %s\n' "$(sha "$f")" "$(basename "$f")"
  done | LC_ALL=C sort -k2
}

case "$mode" in
  generate)
    lines
    ;;
  verify)
    n="$(lines | wc -l | tr -d ' ')"
    [ "$n" -gt 0 ] || { echo "qt-archive-hashes: no archive in $dir (was aqt run with --keep --archive-dest?)" >&2; exit 1; }
    recorded=0
    [ -f "$manifest" ] && grep -v '^#' "$manifest" | grep -q -E "^[0-9a-f]{64}  ${QT_VERSION}-" && recorded=1
    if [ "$recorded" -eq 0 ]; then
      if [ "${QT_ARCHIVES_REQUIRED:-0}" = 1 ]; then
        echo "qt-archive-hashes: no hash recorded for Qt $QT_VERSION in $manifest" >&2
        exit 1
      fi
      echo "::warning::qt-archive-hashes: no hash recorded for Qt $QT_VERSION: the archives are NOT verified against our own list (docs/release-process.md, Qt archive hashes)"
      exit 0
    fi
    bad=0
    lines | while IFS= read -r l; do
      want="$(grep -v '^#' "$manifest" | while read -r h n; do [ "$n" = "${l#*  }" ] && { echo "$h"; break; }; done)"
      if [ -z "$want" ]; then
        echo "qt-archive-hashes: ${l#*  } is not in $manifest" >&2
        echo bad
      elif [ "$want" != "${l%%  *}" ]; then
        echo "qt-archive-hashes: ${l#*  } has the hash ${l%%  *}, the manifest says $want" >&2
        echo bad
      else
        echo "qt-archive-hashes: ok  ${l#*  }" >&2
      fi
    done | grep -q bad && bad=1
    [ "$bad" -eq 0 ] || exit 1
    echo "qt-archive-hashes: $n archives match $manifest"
    ;;
  *)
    echo "usage: qt-archive-hashes.sh verify|generate <archive dir>" >&2
    exit 2
    ;;
esac
