#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of packaging/ci/qt-archive-hashes.sh with fake archives: generate, verify (match, changed archive, archive not
# in the manifest, nothing recorded: warning only, QT_ARCHIVES_REQUIRED=1), and the manifest shipped in the repository.
# Usage: sh tests/test_qt_archive_hashes.sh <repo dir>
set -u
SRC=${1:?usage: test_qt_archive_hashes.sh <repo dir>}
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "test_qt_archive_hashes: FAIL: $*" >&2; exit 1; }
S="$SRC/packaging/ci/qt-archive-hashes.sh"
export QT_VERSION=6.9.3
mkdir -p "$T/a/sub"
printf 'one' > "$T/a/6.9.3-0-202509100000qtbase-Linux.7z"
printf 'two' > "$T/a/sub/6.9.3-0-202509100000qtsvg-Linux.7z"

out=$(sh "$S" generate "$T/a") || fail "generate failed"
[ "$(printf '%s\n' "$out" | wc -l | tr -d ' ')" = 2 ] || fail "generate: two lines expected: $out"
printf '%s\n' "$out" | grep -q -E '^[0-9a-f]{64}  6\.9\.3-0-202509100000qtbase-Linux\.7z$' || fail "generate: line format"

# nothing recorded: a warning, exit 0; with QT_ARCHIVES_REQUIRED=1 a failure
printf '# only comments\n' > "$T/m"
QT_ARCHIVES_MANIFEST="$T/m" sh "$S" verify "$T/a" > "$T/o" 2>&1 || fail "unrecorded: should only warn"
grep -q 'NOT verified' "$T/o" || fail "unrecorded: no warning"
QT_ARCHIVES_REQUIRED=1 QT_ARCHIVES_MANIFEST="$T/m" sh "$S" verify "$T/a" >/dev/null 2>&1 && fail "required but nothing recorded was accepted"
# a manifest of another Qt version counts as nothing recorded
printf '%s  6.8.1-0-2024qtbase-Linux.7z\n' "$(printf x | sha256sum | cut -d' ' -f1)" > "$T/m2"
QT_ARCHIVES_MANIFEST="$T/m2" sh "$S" verify "$T/a" 2>&1 | grep -q 'NOT verified' || fail "another version: should be unrecorded"

# recorded and matching
printf '%s\n' "$out" > "$T/m3"
QT_ARCHIVES_MANIFEST="$T/m3" sh "$S" verify "$T/a" >/dev/null 2>&1 || fail "matching archives refused"
# a changed archive
printf 'changed' > "$T/a/6.9.3-0-202509100000qtbase-Linux.7z"
QT_ARCHIVES_MANIFEST="$T/m3" sh "$S" verify "$T/a" >"$T/o" 2>&1 && fail "a changed archive was accepted"
grep -q 'manifest says' "$T/o" || fail "no reason for the changed archive"
printf 'one' > "$T/a/6.9.3-0-202509100000qtbase-Linux.7z"
# an extra archive that is not in the manifest
printf 'extra' > "$T/a/6.9.3-0-202509100000qtextra-Linux.7z"
QT_ARCHIVES_MANIFEST="$T/m3" sh "$S" verify "$T/a" >"$T/o" 2>&1 && fail "an archive that is not in the manifest was accepted"
grep -q 'not in' "$T/o" || fail "no reason for the unknown archive"
# an empty directory and a missing directory
mkdir "$T/empty"
QT_ARCHIVES_MANIFEST="$T/m3" sh "$S" verify "$T/empty" >/dev/null 2>&1 && fail "an empty archive directory was accepted"
QT_ARCHIVES_MANIFEST="$T/m3" sh "$S" verify "$T/none" >/dev/null 2>&1 && fail "a missing directory was accepted"

# the manifest in the repository: comments and well-formed lines only
grep -v '^#' "$SRC/packaging/ci/qt-archives.sha256" | grep -v -E '^[0-9a-f]{64}  [^ ]+$' | grep -q . && fail "packaging/ci/qt-archives.sha256 has a malformed line"
echo "test_qt_archive_hashes: ok"
