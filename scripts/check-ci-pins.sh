#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Offline check of the CI supply chain pins (docs/security.md, section 14):
#  1. the images of the CI and release builds (packaging/ci/images.env) and the Dockerfile are pinned by digest,
#     and the workflows take every `container:` image from there, never from a tag;
#  2. aqtinstall is installed with hashes only, and packaging/ci/aqt-requirements.txt pins the
#     AQT_VERSION of packaging/qt.env, every package with at least one hash;
#  3. the release job attests the artifacts with a commit-pinned action and has the permissions for it.
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

# 3. attestation of the release
grep -q -E 'uses: actions/attest-build-provenance@[0-9a-f]{40} ' .github/workflows/ci.yml \
  || bad "ci.yml: release job has no commit-pinned actions/attest-build-provenance step"
for p in 'id-token: write' 'attestations: write'; do
  n="$(grep -c "$p" .github/workflows/ci.yml)"
  [ "$n" = 1 ] || bad "ci.yml: '$p' must appear exactly once (only the release job), found $n"
done

[ "$fail" -eq 0 ] || exit 1
echo "check-ci-pins: ok"
