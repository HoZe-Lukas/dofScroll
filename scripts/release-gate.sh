#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Gate of the release job in .github/workflows/ci.yml (issue #263): a tag may publish a release only when
#  (a) the tagged commit is an ancestor of origin/main (a tag pushed on a side branch is refused), and
#  (b) every check that the ruleset of the default branch requires is green on that commit: the newest finished
#      run of each name must have the conclusion "success" (a skipped or missing check is not green).
# The names come from .github/rulesets/main.json (the checked-in copy of the live ruleset), so there is one list.
# Needs a clone with enough history (fetch-depth: 0), git, jq, gh and GH_TOKEN with `checks: read`.
# Usage: scripts/release-gate.sh <commit sha>      (GITHUB_REPOSITORY = owner/name)
set -eu
cd "$(dirname "$0")/.."
sha="${1:?usage: release-gate.sh <commit sha>}"
repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
fail() { echo "release-gate: $*" >&2; exit 1; }

git cat-file -e "$sha^{commit}" 2>/dev/null || fail "$sha is not a commit of this clone"
git rev-parse --verify -q refs/remotes/origin/main >/dev/null || fail "origin/main is not fetched (use fetch-depth: 0)"
git merge-base --is-ancestor "$sha" refs/remotes/origin/main \
  || fail "the tagged commit $sha is not on main: merge it to main first, then tag"
echo "release-gate: $sha is an ancestor of origin/main"

names="$(jq -r '.rules[] | select(.type == "required_status_checks") | .parameters.required_status_checks[].context' \
  .github/rulesets/main.json)"
[ -n "$names" ] || fail "no required checks found in .github/rulesets/main.json"
runs="$(gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" \
  --jq '.check_runs[] | [.name, (.completed_at // ""), .status, (.conclusion // "")] | @tsv')" \
  || fail "cannot read the check runs of $sha"

bad=0
# the names contain spaces, so split on newlines only
oldifs="$IFS"
IFS='
'
for name in $names; do
  IFS="$oldifs"
  last="$(printf '%s\n' "$runs" | awk -F '\t' -v n="$name" '$1 == n && $3 == "completed" { print $2 "\t" $4 }' | sort | tail -n 1)"
  conclusion="${last#*	}"
  if [ -z "$last" ] || [ "$conclusion" != "success" ]; then
    echo "release-gate: required check not green on $sha: $name (${conclusion:-no finished run})" >&2
    bad=1
  else
    echo "release-gate: ok  $name"
  fi
  IFS='
'
done
IFS="$oldifs"
[ "$bad" -eq 0 ] || fail "not every required check is green; fix main first, then tag"
echo "release-gate: passed"
