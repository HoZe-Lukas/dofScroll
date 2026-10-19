#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Links the repo's pre-commit hook: .git/hooks/pre-commit -> scripts/pre-commit
#
# Usage:  scripts/install-hooks.sh [--force]
#   - idempotent: if the link is already set, nothing happens
#   - an existing foreign hook is never silently overwritten: interactively the user
#     is asked, without a terminal only with --force; in both cases it is backed
#     up to pre-commit.bak.<timestamp> first
#   - bypassing the hook in a single case: git commit --no-verify

set -eu

force=0
case "${1:-}" in
  "") ;;
  --force) force=1 ;;
  -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
  *) echo "Usage: $0 [--force]" >&2; exit 2 ;;
esac

top=$(git rev-parse --show-toplevel 2>/dev/null) || {
  echo "install-hooks: not in a Git repository" >&2
  exit 1
}
src="$top/scripts/pre-commit"
[ -f "$src" ] || { echo "install-hooks: $src is missing" >&2; exit 1; }
chmod +x "$src"

if hp=$(git config --get core.hooksPath 2>/dev/null) && [ -n "$hp" ]; then
  echo "install-hooks: WARNING - core.hooksPath=$hp is set; Git then uses" >&2
  echo "  NOT .git/hooks. Install the hook there yourself or remove core.hooksPath." >&2
fi

# Hook directory (also handles worktrees / a different GIT_DIR)
hooks=$(git rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$(pwd)/$hooks" ;; esac
mkdir -p "$hooks"
dst="$hooks/pre-commit"

# Relative link if the hook directory is the usual <repo>/.git/hooks
# (survives moving the repo), otherwise absolute.
if [ "$(cd "$hooks" && pwd -P)" = "$(cd "$top" && pwd -P)/.git/hooks" ]; then
  link="../../scripts/pre-commit"
else
  link="$src"
fi

if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$link" ]; then
  echo "install-hooks: already installed ($dst -> $link)"
  exit 0
fi

if [ -e "$dst" ] || [ -L "$dst" ]; then
  echo "install-hooks: a different hook already exists: $dst"
  if [ -L "$dst" ]; then echo "  (Symlink -> $(readlink "$dst"))"; fi
  if [ "$force" -ne 1 ]; then
    if [ -t 0 ] && [ -t 1 ]; then
      printf "Back up and replace with a link to scripts/pre-commit? [y/N] "
      read -r ans || ans=""
      case "$ans" in j|J|y|Y|ja|Ja|yes) ;; *) echo "aborted, nothing changed."; exit 1 ;; esac
    else
      echo "install-hooks: no terminal - nothing changed (replace with --force)." >&2
      exit 1
    fi
  fi
  bak="$dst.bak.$(date +%Y%m%d%H%M%S)"
  mv "$dst" "$bak"
  echo "install-hooks: old hook backed up to $bak"
fi

ln -s "$link" "$dst"
echo "install-hooks: installed ($dst -> $link)"
