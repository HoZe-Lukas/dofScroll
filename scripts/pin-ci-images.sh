#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# The container images of the release builds are pinned by digest (name[:tag]@sha256:...), so a
# replaced tag cannot change what goes into the RPM, DEB and TGZ. This script looks up the digest that
# the tag of every pinned reference has now and either rewrites the files or only reports.
#
#   scripts/pin-ci-images.sh           rewrite the digests (review the diff, commit)
#   scripts/pin-ci-images.sh --check   exit 1 and list every digest that is out of date (no changes)
#
# Pinned references live in FILES below (not in the workflow files: they read packaging/ci/images.env, so the
# refresh pull request changes no workflow file and needs no token with the `workflows` permission). A reference without a tag (our own images, which are
# published as :latest by .github/workflows/ci-image.yml) is looked up as :latest.
# Procedure after the monthly ci-image run or a change of packaging/ci/: run this script, commit the
# new digests (docs/releasing.md). Needs skopeo (or crane) and network access; it never invents a digest.
set -eu
cd "$(dirname "$0")/.."
mode=update
[ "${1:-}" = "--check" ] && mode=check

FILES="packaging/ci/images.env packaging/ci/rocky9.Dockerfile"

if command -v skopeo >/dev/null 2>&1; then
  lookup() { skopeo inspect --no-tags "docker://$1" | sed -n 's/^ *"Digest": *"\(sha256:[0-9a-f]\{64\}\)".*/\1/p' | head -1; }
elif command -v crane >/dev/null 2>&1; then
  lookup() { crane digest "$1"; }
else
  echo "pin-ci-images: skopeo or crane is needed" >&2; exit 2
fi

stale=0
for f in $FILES; do
  for ref in $(grep -o '[A-Za-z0-9._/-]*\(:[A-Za-z0-9._-]*\)\{0,1\}@sha256:[0-9a-f]\{64\}' "$f" | sort -u); do
    old="${ref##*@}"
    name="${ref%@*}"
    case "$name" in *:*) lookupref="$name" ;; *) lookupref="$name:latest" ;; esac
    case "$lookupref" in */*) full="$lookupref" ;; *) full="docker.io/library/$lookupref" ;; esac
    case "${full%%/*}" in *.*) ;; *) full="docker.io/$full" ;; esac
    new="$(lookup "$full")"
    if ! printf '%s' "$new" | grep -q '^sha256:[0-9a-f]\{64\}$'; then
      echo "pin-ci-images: cannot look up $full, nothing changed for it" >&2; exit 2
    fi
    if [ "$new" = "$old" ]; then
      echo "ok       $name  $old"
    else
      stale=1
      echo "outdated $name  $old -> $new  ($f)"
      [ "$mode" = update ] && sed -i "s|$name@$old|$name@$new|g" "$f"
    fi
  done
done

if [ "$mode" = check ] && [ "$stale" -ne 0 ]; then
  echo "pin-ci-images: run scripts/pin-ci-images.sh and commit the result" >&2
  exit 1
fi
exit 0
