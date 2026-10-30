#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Gate of the job `propose` in .github/workflows/refresh-ci-pins.yml (issues #262, audit round 2): the files taken over
# from the generator job may differ from the checked-in ones in image digests and requirement pins/hashes only. Run
# in the work tree after the three files were copied in; it compares the work tree with HEAD.
#
# Every changed line (added or removed) must have exactly the shape of a digest or a pin; the hunk headers and the
# file headers of the diff are skipped by position (not by their first characters), because a changed line whose
# content starts with "-" or "+" (for example "--index-url ..." in the requirements) shows up in the diff as "+--" /
# "+++" and must not be mistaken for a header.
# Usage: sh scripts/check-pin-diff.sh        (exit 1 and the offending lines on stderr)
set -eu
cd "$(git rev-parse --show-toplevel)"

# The changed lines of the given files, without any header.
changed() {
  git diff --no-color --no-ext-diff --no-renames -U0 -- "$@" | awk '
    /^diff --git / { header = 1; next }
    /^@@ /         { header = 0; next }
    !header && /^[-+]/ { print }'
}

bad=0
# images.env: KEY=image@sha256:<64 hex>; Dockerfile: the FROM line with a digest (and its stage name)
changed packaging/ci/images.env packaging/ci/rocky9.Dockerfile \
  | grep -v -E '^[-+]([A-Z0-9_]+=[^ ]+@sha256:[0-9a-f]{64}|FROM [^ ]+@sha256:[0-9a-f]{64}( AS [a-z0-9_-]+)?)$' >&2 \
  && { echo "check-pin-diff: a changed line of images.env or rocky9.Dockerfile is not a digest" >&2; bad=1; }
# aqt-requirements.txt: "name==version[ ; python_version < "x.y"] \" and "    --hash=sha256:<64 hex>[ \]"
changed packaging/ci/aqt-requirements.txt \
  | grep -v -E '^[-+]([A-Za-z0-9_][A-Za-z0-9_.-]*==[0-9A-Za-z.!+_-]+( ; python_version < "[0-9.]+")? \\| +--hash=sha256:[0-9a-f]{64}( \\)?)$' >&2 \
  && { echo "check-pin-diff: a changed line of aqt-requirements.txt is not a pin or a hash" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
echo "check-pin-diff: ok"
