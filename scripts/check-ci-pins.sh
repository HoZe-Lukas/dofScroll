#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Offline check of the CI supply chain pins (docs/security.md, section 14):
#  1. the images of the CI and release builds (packaging/ci/images.env) and the Dockerfile are pinned by digest,
#     and the workflows take every `container:` image from there, never from a tag;
#  2. aqtinstall is installed with hashes only, and packaging/ci/aqt-requirements.txt pins the
#     AQT_VERSION of packaging/qt.env, every package with at least one hash;
#  3. refresh-ci-pins.yml keeps code from registries away from the write token (two jobs, no `git add -A`);
#  4. the release job has the environment "release", runs scripts/release-gate.sh (checks: read) and attests the artifacts with a commit-pinned action and has the permissions for it.
# Usage: scripts/check-ci-pins.sh
set -eu
cd "$(dirname "$0")/.."
fail=0
bad() { echo "check-ci-pins: $*" >&2; fail=1; }

# 1. images by digest
[ -f packaging/ci/images.env ] || bad "packaging/ci/images.env is missing"
grep -E '^[A-Z0-9_]+=' packaging/ci/images.env | grep -v -E '^[A-Z0-9_]+=[^ ]+@sha256:[0-9a-f]{64}$' \
  && bad "images.env: an image is not pinned by digest (scripts/pin-ci-images.sh)"
grep -h -E '^ +container: ' .github/workflows/*.yml | grep -v -F 'container: ${{ needs.pins.outputs.' \
  && bad "a workflow takes a container image from somewhere else than packaging/ci/images.env (job pins)"
grep -E '^FROM ' packaging/ci/rocky9.Dockerfile | grep -v -E '^FROM [^ ]*@sha256:[0-9a-f]{64}( |$)' \
  | grep -v -E '^FROM (base|qt)( |$)' \
  && bad "rocky9.Dockerfile: a FROM line is not pinned by digest (scripts/pin-ci-images.sh)"

# 2. aqtinstall with hashes
. packaging/qt.env
req=packaging/ci/aqt-requirements.txt
grep -q -i "^aqtinstall==${AQT_VERSION} " "$req" \
  || bad "$req does not pin aqtinstall==$AQT_VERSION (scripts/update-aqt-requirements.sh)"
if awk '/^[A-Za-z0-9_.-]+==/ { if (name != "" && hashes == 0) exit 1; name = $1; hashes = 0; next }
        /--hash=sha256:/ { hashes++ }
        END { if (name != "" && hashes == 0) exit 1 }' "$req"; then :; else bad "$req: a package without hash"; fi
if grep -rn -E 'pip3? install' .github/workflows packaging/ci scripts 2>/dev/null \
   | grep -i aqtinstall | grep -v -e '--require-hashes' -e 'check-ci-pins.sh' -e 'update-aqt-requirements.sh'; then
  bad "aqtinstall installed without --require-hashes"
fi
grep -rn -E 'pip3? install.*"?aqtinstall==' .github/workflows packaging/ci && bad "aqtinstall pinned by version only"

# 3. refresh-ci-pins.yml: the job that runs the generators has a read-only token, the job with the write token
#    never runs them and commits named files only
wf=.github/workflows/refresh-ci-pins.yml
for p in 'contents: write' 'pull-requests: write' 'actions: write'; do
  n="$(grep -c "^      $p" "$wf")"
  [ "$n" = 1 ] || bad "$wf: '$p' must appear exactly once (only the job propose), found $n"
done
grep -v -E '^ *#' "$wf" | grep -i -E 'git add +(-A|--all|\.)' && bad "$wf: commit named files only, never git add -A"
split="$(grep -n -E '^  propose:' "$wf" | cut -d: -f1)"
[ -n "$split" ] || bad "$wf: job propose is missing"
if [ -n "$split" ]; then
  tail -n "+$split" "$wf" | grep -v -E '^ *#' | grep -E 'sh scripts/(pin-ci-images|update-aqt-requirements)\.sh|docker run|pip3? ' \
    && bad "$wf: the generators run in the read-only job only"
  head -n "$((split - 1))" "$wf" | grep -v -E '^ *#' | grep -E 'GH_TOKEN|github\.token|REFRESH_PINS_TOKEN' \
    && bad "$wf: no token in the read-only job"
fi

# the job that holds the write token lets only digests, pins and hashes through (scripts/check-pin-diff.sh)
grep -v -E '^ *#' "$wf" | grep -q -E 'sh scripts/check-pin-diff\.sh' \
  || bad "$wf: the job propose must run scripts/check-pin-diff.sh on the generated files"
grep -v -E '^ *#' "$wf" | grep -E 'git diff -U0' && bad "$wf: judge the generated files with scripts/check-pin-diff.sh (a hand-written diff filter lets lines through)"

# 4. attestation of the release
grep -q -E 'uses: actions/attest-build-provenance@[0-9a-f]{40} ' .github/workflows/ci.yml \
  || bad "ci.yml: release job has no commit-pinned actions/attest-build-provenance step"
for p in 'id-token: write' 'attestations: write'; do
  n="$(grep -c "$p" .github/workflows/ci.yml)"
  [ "$n" = 1 ] || bad "ci.yml: '$p' must appear exactly once (only the release job), found $n"
done

# 5. release gate: environment, gate step and the read permission, only in the release job (issue #263)
grep -q -E '^    environment: release$' .github/workflows/ci.yml \
  || bad "ci.yml: the release job must have 'environment: release'"
grep -q -E 'run: sh scripts/release-gate\.sh "\$GITHUB_SHA"' .github/workflows/ci.yml \
  || bad "ci.yml: the release job must run scripts/release-gate.sh"
n="$(grep -c 'environment:' .github/workflows/ci.yml)"
[ "$n" = 1 ] || bad "ci.yml: 'environment:' must appear exactly once (only the release job), found $n"
grep -q -E '^      checks: read ' .github/workflows/ci.yml || bad "ci.yml: the release job needs 'checks: read' for the gate"
grep -q 'creation' .github/rulesets/tags.json || bad ".github/rulesets/tags.json: the creation rule is missing"
grep -q '"Integration"' .github/rulesets/tags.json || bad ".github/rulesets/tags.json: the GitHub Actions app must be a bypass actor (tag-release.yml)"

# 6. Qt archives: every aqt install-qt keeps its archives and is followed by the hash check (issue #265)
inst="$( { grep -rn -E 'aqt"?( |/aqt")? ?install-qt |aqt install-qt ' .github/workflows packaging/ci/rocky9.Dockerfile packaging/ci/debian12-qt.sh 2>/dev/null || true; } \
  | grep -v -E '^[^:]*:[0-9]+: *#' | grep -v -e 'qt-update-check' -e 'qt-archive-hashes.yml' || true)"
n_inst="$(printf '%s\n' "$inst" | grep -c 'install-qt ' || true)"
n_keep="$(printf '%s\n' "$inst" | grep -c -e '--keep --archive-dest' || true)"
n_ver="$(grep -rn -E 'qt-archive-hashes\.sh"? verify' .github/workflows packaging/ci/rocky9.Dockerfile packaging/ci/debian12-qt.sh | grep -v -E ':[0-9]+: *#' | grep -c . || true)"
[ "$n_inst" -gt 0 ] && [ "$n_inst" = "$n_keep" ] && [ "$n_inst" = "$n_ver" ] \
  || bad "every 'aqt install-qt' needs '--keep --archive-dest' and a 'qt-archive-hashes.sh verify' (found $n_inst installs, $n_keep with --keep, $n_ver checks)"

[ "$fail" -eq 0 ] || exit 1
echo "check-ci-pins: ok"
