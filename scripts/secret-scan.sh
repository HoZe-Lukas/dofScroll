#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Simple secret scan over the ADDED lines of a git diff (POSIX sh).
#
# Usage: scripts/secret-scan.sh [git-diff-args...]
#   default (no args):  staged changes (git diff --cached), as used by scripts/pre-commit
#   CI example:         scripts/secret-scan.sh origin/main...HEAD
# Exit 0 = nothing found, 1 = possible secrets (printed), 2 = not in a Git repository.
#
# The patterns are written ([Y], [d] ...) so that this script does not
# flag itself as a match when committed.

set -u

top=$(git rev-parse --show-toplevel 2>/dev/null) || {
  echo "secret-scan: not in a Git repository" >&2
  exit 2
}
cd "$top" || exit 2

[ $# -gt 0 ] || set -- --cached

# Added lines only. File headers are recognized by state (the header ends at the
# first '@@'), NOT by the line prefix: an added line with the content '++ x'
# appears as '+++ x' and must not be skipped as a header.
# --no-prefix: file name without 'a/'/'b/'.
added=$(git diff "$@" --no-color --no-ext-diff --no-prefix -U0 --diff-filter=ACMR 2>/dev/null |
  awk '
    /^diff --git /      { hdr = 1; next }
    hdr && /^\+\+\+ /   { f = substr($0, 5); next }
    /^@@/               { hdr = 0; next }
    !hdr && /^\+/       { print f ":" substr($0, 2) }
  ')

hits=""
check() {
  # $1 = description, $2 = grep -E pattern, $3 = optional grep flags (e.g. -i)
  m=$(printf '%s\n' "$added" | grep -E ${3:-} -- "$2" | cut -c1-160)
  if [ -n "$m" ]; then
    hits="${hits}
--- $1:
$m"
  fi
}

if [ -n "$added" ]; then
  # Keywords split in expressions like [Y]/[d]/[t] so that this script does not match itself.
  check "Private key"   '-----BEGIN ([A-Z0-9]+ )*PRIVATE KE[Y]'
  check "GitLab token (glpat)"  'glpa[t]-[A-Za-z0-9_-]{20,}'
  check "GitHub token (ghp_)"   'gh[pousr]_[A-Za-z0-9]{30,}'
  check "Slack token"           'xo[x][baprs]-[A-Za-z0-9-]{10,}'
  check "AWS access key"        '(AKI[A]|ASI[A])[0-9A-Z]{16}'
  check "JWT"                   'eyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]+'
  # Assignment with '=' or ':' (also JSON "key": "value"); placeholders/variables excluded
  check "Secret assignment"   '(passwor[d]|passw[d]|secre[t]|toke[n]|api[_-]?ke[y])["'"'"']?[[:space:]]*[:=][[:space:]]*["'"'"']?[A-Za-z0-9/+_.~-]{8,}' -i
fi

if [ -n "$hits" ]; then
  echo "secret-scan: possible secrets in the added lines:" >&2
  printf '%s\n' "$hits" >&2
  exit 1
fi
exit 0
