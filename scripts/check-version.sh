#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# The version is set in CMakeLists.txt (project VERSION). Everything that repeats it by hand must follow it in the same
# change that bumps it (docs/releasing.md); this offline check fails when one does not:
#  - the application manifest of the Windows programs (packaging/windows/dofscroll.manifest: <version>.0),
#  - the release notes of that version (docs/release-notes-<version>.md),
#  - the status line of readme.md ("Current stable version") and of the wiki home page,
#  - the structured data of the English and the German start page of the website (softwareVersion).
# Usage: scripts/check-version.sh
set -eu
cd "$(dirname "$0")/.."
fail=0
bad() { echo "check-version: $*" >&2; fail=1; }

ver=$(sed -n 's/^project(dofscroll VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)
[ -n "$ver" ] || { echo "check-version: no project VERSION in CMakeLists.txt" >&2; exit 1; }

grep -q "name=\"dofscroll.daemon\" version=\"$ver.0\"" packaging/windows/dofscroll.manifest \
  || bad "packaging/windows/dofscroll.manifest: assemblyIdentity version is not $ver.0"
[ -f "docs/release-notes-$ver.md" ] || bad "docs/release-notes-$ver.md is missing"
grep -q "Current stable version: \*\*$ver\*\*" readme.md || bad "readme.md: 'Current stable version' is not $ver"
grep -q "Status: version $ver\." docs/wiki/Home.md || bad "docs/wiki/Home.md: 'Status: version' is not $ver"
for f in site/index.html site/de/index.html; do
  grep -q "\"softwareVersion\": \"$ver\"" "$f" || bad "$f: softwareVersion is not $ver"
done

[ "$fail" = 0 ] && echo "check-version: OK ($ver)"
exit "$fail"
