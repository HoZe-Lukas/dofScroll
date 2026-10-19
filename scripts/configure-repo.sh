#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Applies the repository settings described in docs/repo-settings.md through the GitHub REST
# API (`gh api`). Idempotent: every call sets a desired state (PATCH/PUT) or updates the
# ruleset of the same name, so it can be re-run at any time.
#
#   scripts/configure-repo.sh            dry run: only prints the gh api calls
#   scripts/configure-repo.sh --apply    executes them (needs `gh auth login` with admin rights
#                                        on the repository)
#   REPO=owner/name scripts/configure-repo.sh   target another repository
#
# Features that need a public repository or a paid plan (rulesets, secret scanning, ...) answer
# with HTTP 403/404/422 on a private free-plan repository. Those calls are reported as
# "skipped" and the script continues; run it again after the repository became public.
# Any other error is reported as "FAILED" and makes the exit status non-zero.
set -u

REPO="${REPO:-HoZe-Lukas/dofScroll}"
apply=0
case "${1:-}" in
  --apply) apply=1 ;;
  "") ;;
  -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) echo "usage: $0 [--apply]" >&2; exit 2 ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd)
rulesets="$root/.github/rulesets"
failed=0
skipped=0

if [ "$apply" -eq 1 ]; then
  command -v gh >/dev/null 2>&1 || { echo "gh CLI not found" >&2; exit 2; }
  echo "== APPLY mode, repository: $REPO"
else
  echo "== DRY RUN (nothing is changed; use --apply), repository: $REPO"
fi

# call DESCRIPTION METHOD ENDPOINT [JSON_BODY]
# The JSON body, if given, is sent on stdin (gh api --input -).
call() {
  desc=$1; method=$2; endpoint=$3; body=${4:-}
  echo
  echo "# $desc"
  if [ -n "$body" ]; then
    echo "gh api -X $method repos/$REPO$endpoint --input - <<'JSON'"
    echo "$body"
    echo "JSON"
  else
    echo "gh api -X $method repos/$REPO$endpoint"
  fi
  [ "$apply" -eq 1 ] || return 0
  if [ -n "$body" ]; then
    out=$(printf '%s' "$body" | gh api -X "$method" "repos/$REPO$endpoint" --input - 2>&1)
  else
    out=$(gh api -X "$method" "repos/$REPO$endpoint" 2>&1)
  fi
  rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "  -> ok"
    return 0
  fi
  case "$out" in
    *"HTTP 403"*|*"HTTP 404"*|*"HTTP 422"*)
      echo "  -> SKIPPED (not available for this repository/plan, or already in the requested state):"
      echo "     $(printf '%s' "$out" | head -n 2 | tr '\n' ' ')"
      skipped=$((skipped + 1)) ;;
    *)
      echo "  -> FAILED: $(printf '%s' "$out" | head -n 3 | tr '\n' ' ')"
      failed=$((failed + 1)) ;;
  esac
  return 0
}

# upsert_ruleset FILE: update the ruleset of the same name, else create it.
upsert_ruleset() {
  file=$1
  name=$(sed -n 's/^  "name": "\(.*\)",$/\1/p' "$file" | head -n 1)
  [ -n "$name" ] || { echo "no name in $file" >&2; failed=$((failed + 1)); return; }
  body=$(cat "$file")
  if [ "$apply" -eq 1 ]; then
    id=$(gh api "repos/$REPO/rulesets" --jq ".[] | select(.name == \"$name\") | .id" 2>/dev/null | head -n 1)
    if [ -n "$id" ]; then
      call "ruleset '$name': update existing (id $id)" PUT "/rulesets/$id" "$body"
    else
      call "ruleset '$name': create" POST "/rulesets" "$body"
    fi
  else
    echo
    echo "# ruleset '$name': PUT /rulesets/<id> if a ruleset of that name exists, else POST /rulesets"
    echo "gh api repos/$REPO/rulesets --jq '.[] | select(.name == \"$name\") | .id'"
    echo "gh api -X POST|PUT repos/$REPO/rulesets[/<id>] --input $file"
  fi
}

# --- repository options -------------------------------------------------------------------
call "repository options: squash-only merges, auto-merge, delete head branches, wiki on, no projects" \
  PATCH "" '{
  "allow_squash_merge": true,
  "allow_merge_commit": false,
  "allow_rebase_merge": false,
  "allow_auto_merge": true,
  "delete_branch_on_merge": true,
  "allow_update_branch": true,
  "squash_merge_commit_title": "PR_TITLE",
  "squash_merge_commit_message": "PR_BODY",
  "has_wiki": true,
  "has_projects": false
}'

# --- security features ---------------------------------------------------------------------
call "secret scanning + push protection" PATCH "" '{
  "security_and_analysis": {
    "secret_scanning": { "status": "enabled" },
    "secret_scanning_push_protection": { "status": "enabled" }
  }
}'
call "Dependabot alerts" PUT "/vulnerability-alerts"
call "Dependabot security updates" PUT "/automated-security-fixes"
call "private vulnerability reporting" PUT "/private-vulnerability-reporting"

# --- Actions ---------------------------------------------------------------------------------
call "Actions: default GITHUB_TOKEN read-only, Actions may not approve PRs" PUT "/actions/permissions/workflow" '{
  "default_workflow_permissions": "read",
  "can_approve_pull_request_reviews": false
}'
call "Actions: selected actions only, full-SHA pinning required" PUT "/actions/permissions" '{
  "enabled": true,
  "allowed_actions": "selected",
  "sha_pinning_required": true
}'
# GitHub-owned actions (actions/*, github/*) are allowed as a group; the rest is listed.
# oven-sh/setup-bun is used internally by anthropics/claude-code-action (composite action).
call "Actions: allow-list" PUT "/actions/permissions/selected-actions" '{
  "github_owned_allowed": true,
  "verified_allowed": false,
  "patterns_allowed": [
    "anthropics/claude-code-action@*",
    "jurplel/install-qt-action@*",
    "oven-sh/setup-bun@*"
  ]
}'
call "Actions: approval required for ALL outside contributors' workflow runs" PUT \
  "/actions/permissions/fork-pr-contributor-approval" '{ "approval_policy": "all_external_contributors" }'

# --- rulesets (last, so a half-applied run never locks the maintainer out) ---------------------
upsert_ruleset "$rulesets/main.json"
upsert_ruleset "$rulesets/tags.json"

echo
if [ "$apply" -eq 1 ]; then
  echo "== done: $skipped skipped, $failed failed"
  [ "$failed" -eq 0 ] || exit 1
else
  echo "== dry run finished; re-run with --apply to execute"
fi
