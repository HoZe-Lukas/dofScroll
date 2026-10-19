#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Publishes docs/wiki/*.md to the GitHub wiki of this repository.
#
# Usage: scripts/sync-wiki.sh [--push] [--prune] [--repo OWNER/REPO]
#
#   (no flag)   dry run: clones the wiki, copies the pages, shows what would change, pushes nothing
#   --push      commit and push the change
#   --prune     also delete *.md pages in the wiki that are not in docs/wiki (default: keep them)
#   --repo      OWNER/REPO (default: $GITHUB_REPOSITORY, else derived from the "origin" remote)
#
# Authentication: if GITHUB_TOKEN (or WIKI_TOKEN, which wins) is set it is sent as an HTTP
# header (never written to the clone URL or to disk). Otherwise git uses your own credentials
# (credential helper or SSH-less HTTPS prompt is disabled, so configure a helper first).
#
# The SPDX comment lines at the top of the pages are removed before publishing.
#
# Exit codes: 0 ok (also "nothing to do"), 1 error, 2 usage, 3 the wiki repository cannot be
# cloned (it does not exist yet, or the credentials have no access).
set -eu

push=0
prune=0
repo=${GITHUB_REPOSITORY:-}

while [ $# -gt 0 ]; do
  case "$1" in
    --push) push=1 ;;
    --prune) prune=1 ;;
    --repo)
      [ $# -ge 2 ] || { echo "sync-wiki: --repo needs OWNER/REPO" >&2; exit 2; }
      repo=$2
      shift
      ;;
    -h|--help) sed -n '4,22p' "$0"; exit 0 ;;
    *) echo "sync-wiki: unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

root=$(git rev-parse --show-toplevel)
src="$root/docs/wiki"
[ -d "$src" ] || { echo "sync-wiki: $src does not exist" >&2; exit 1; }

if [ -z "$repo" ]; then
  url=$(git -C "$root" remote get-url origin 2>/dev/null || true)
  repo=$(printf '%s\n' "$url" | sed -n -e 's#^.*github\.com[:/]\([^/]*/[^/]*\)$#\1#p' | sed 's#\.git$##')
fi
case "$repo" in
  */*) ;;
  *) echo "sync-wiki: cannot determine OWNER/REPO, pass --repo" >&2; exit 2 ;;
esac

token=${WIKI_TOKEN:-${GITHUB_TOKEN:-}}
GIT_TERMINAL_PROMPT=0
export GIT_TERMINAL_PROMPT
wiki_url="https://github.com/$repo.wiki.git"

# Run git with the token as an extra header when one is available.
wgit() {
  if [ -n "$token" ]; then
    cred=$(printf 'x-access-token:%s' "$token" | base64 | tr -d '\n')
    git -c "http.extraheader=Authorization: Basic $cred" "$@"
  else
    git "$@"
  fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
clone="$tmp/wiki"

if ! wgit clone --quiet "$wiki_url" "$clone" 2>"$tmp/clone.err"; then
  {
    echo "sync-wiki: cannot clone $wiki_url"
    echo "  The wiki repository does not exist until its first page is created."
    echo "  Enable the wiki in the repository settings and create the first page"
    echo "  (any content) in the browser once, then run this again."
    echo "  It can also mean that the credentials have no access."
    echo "  git said:"
    sed 's/^/    /' "$tmp/clone.err"
  } >&2
  exit 3
fi

count=0
for f in "$src"/*.md; do
  [ -f "$f" ] || continue
  name=$(basename "$f")
  # Drop whole-line SPDX HTML comments; the wiki pages carry no license headers.
  sed '/^<!-- SPDX-[A-Za-z-]*:.*-->$/d' "$f" > "$clone/$name"
  count=$((count + 1))
done
if [ "$count" -eq 0 ]; then
  echo "sync-wiki: no pages in $src" >&2
  exit 1
fi

if [ "$prune" -eq 1 ]; then
  for f in "$clone"/*.md; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    [ -f "$src/$name" ] || { echo "sync-wiki: pruning $name"; rm -f "$f"; }
  done
fi

git -C "$clone" add -A
if git -C "$clone" diff --cached --quiet; then
  echo "sync-wiki: wiki is already up to date ($count pages)"
  exit 0
fi

git -C "$clone" status --short
if [ "$push" -ne 1 ]; then
  echo "sync-wiki: dry run, nothing committed or pushed (use --push)"
  exit 0
fi

rev=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)
git -C "$clone" \
  -c user.name="${GIT_AUTHOR_NAME:-github-actions[bot]}" \
  -c user.email="${GIT_AUTHOR_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}" \
  commit --quiet -m "Sync wiki from docs/wiki at $rev"
wgit -C "$clone" push --quiet origin HEAD
echo "sync-wiki: pushed $count pages to $wiki_url"
