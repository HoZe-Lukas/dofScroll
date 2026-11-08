#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of scripts/secret-scan.sh with a throw-away git repository: every kind of token it claims to find is found,
# harmless text is not. The test strings are put together at run time so that this file does not match itself
# (the CI secret scan reads the whole tree).
# Usage: sh tests/test_secret_scan.sh <repo dir>
set -u
SRC=${1:?usage: test_secret_scan.sh <repo dir>}
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
unset GIT_DIR GIT_INDEX_FILE GIT_WORK_TREE GIT_PREFIX GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
fail() { echo "test_secret_scan: FAIL: $*" >&2; exit 1; }

mkdir -p "$T/w/scripts"
cp "$SRC/scripts/secret-scan.sh" "$T/w/scripts/"
cd "$T/w" || exit 1
git init -q . && git config user.email t@example.invalid && git config user.name t
git commit -q --allow-empty -m base

p36=abcdefghijklmnopqrstuvwxyz0123456789
scan() {  # $1 = the added line; exit status of the scan
  printf '%s\n' "$1" > f.txt
  git add f.txt
  sh scripts/secret-scan.sh >"$T/out" 2>&1
  rc=$?
  git rm -q --cached -f f.txt
  return $rc
}
must_find() { scan "$2" && fail "not found: $1"; true; }
must_pass() { scan "$2" || fail "false positive: $1: $(cat "$T/out")"; true; }

must_find "classic GitHub token" "x gh""p_$p36"
must_find "fine-grained GitHub token" "x github""_pat_11ABCDEFG0abcdefghijkl_$p36$p36"
must_find "GitLab token" "x glp""at-abcdefghij0123456789"
must_find "private key" "-----BEGIN RSA PRIVATE ""KEY-----"
must_find "password assignment" "pass""word = 'hunter2hunter2'"
must_pass "plain text" "github is a site, and a pat is an animal"
must_pass "short word after github_pat_" "see github""_pat_ in the docs"
echo "test_secret_scan: OK"
