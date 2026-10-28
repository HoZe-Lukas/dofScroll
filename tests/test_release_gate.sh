#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of scripts/release-gate.sh with a throw-away git repository and a fake gh: ancestor of main or not,
# required checks green, failed, skipped, missing, rerun (the newest run counts).
# Usage: sh tests/test_release_gate.sh <repo dir>
set -u
SRC=${1:?usage: test_release_gate.sh <repo dir>}
command -v jq >/dev/null 2>&1 || { echo "test_release_gate: skipped (no jq)"; exit 0; }
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
# Under a git hook the GIT_* variables point at the real repository: the throw-away repo must not see them.
unset GIT_DIR GIT_INDEX_FILE GIT_WORK_TREE GIT_PREFIX GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
fail() { echo "test_release_gate: FAIL: $*" >&2; exit 1; }

mkdir -p "$T/w/scripts" "$T/w/.github/rulesets" "$T/bin"
cp "$SRC/scripts/release-gate.sh" "$T/w/scripts/"
cp "$SRC/.github/rulesets/main.json" "$T/w/.github/rulesets/"
cd "$T/w" || exit 1
git init -q . && git config user.email t@example.invalid && git config user.name t
git add . && git commit -q -m base
git update-ref refs/remotes/origin/main HEAD
main_sha=$(git rev-parse HEAD)
git checkout -q -b side && echo x > x && git add x && git commit -q -m side
side_sha=$(git rev-parse HEAD)

# fake gh: prints the tab-separated check runs from $FAKE_RUNS (name, completed_at, status, conclusion)
cat > "$T/bin/gh" <<'EOS'
#!/bin/sh
[ "${FAKE_GH_FAIL:-0}" = 1 ] && exit 1
cat "$FAKE_RUNS"
EOS
chmod +x "$T/bin/gh"
names=$(jq -r '.rules[] | select(.type == "required_status_checks") | .parameters.required_status_checks[].context' .github/rulesets/main.json)
[ -n "$names" ] || fail "no names in the ruleset"
all_green() { printf '%s\n' "$names" | while IFS= read -r n; do printf '%s\t2026-10-28T10:00:00Z\tcompleted\tsuccess\n' "$n"; done; }
gate() { PATH="$T/bin:$PATH" GITHUB_REPOSITORY=o/r FAKE_RUNS="$T/runs" sh scripts/release-gate.sh "$1" >"$T/out" 2>&1; }

all_green > "$T/runs"
gate "$main_sha" || fail "green main commit refused: $(cat "$T/out")"
gate "$side_sha" && fail "a commit that is not on main was accepted"
grep -q "not on main" "$T/out" || fail "no reason for the side commit"

first=$(printf '%s\n' "$names" | head -n 1)
# a failed check
all_green | sed "1s/success/failure/" > "$T/runs"
gate "$main_sha" && fail "a failed check was accepted"
# a skipped check
all_green | sed "1s/success/skipped/" > "$T/runs"
gate "$main_sha" && fail "a skipped check was accepted"
# a missing check
all_green | tail -n +2 > "$T/runs"
gate "$main_sha" && fail "a missing check was accepted"
grep -q "no finished run" "$T/out" || fail "missing check not reported"
# an unfinished check
all_green | sed "1s/completed/in_progress/" > "$T/runs"
gate "$main_sha" && fail "an unfinished check was accepted"
# failure first, newer success: the newest finished run counts
{ all_green; printf '%s\t2026-10-28T09:00:00Z\tcompleted\tfailure\n' "$first"; } > "$T/runs"
gate "$main_sha" || fail "a rerun that succeeded was refused: $(cat "$T/out")"
# success first, newer failure
{ all_green; printf '%s\t2026-10-28T11:00:00Z\tcompleted\tfailure\n' "$first"; } > "$T/runs"
gate "$main_sha" && fail "a newer failure was ignored"
# an unrelated failing check does not matter
{ all_green; printf 'Windows (MSVC) build\t2026-10-28T11:00:00Z\tcompleted\tfailure\n'; } > "$T/runs"
gate "$main_sha" || fail "an unrelated check blocked the release"
# gh failing
all_green > "$T/runs"
FAKE_GH_FAIL=1 gate "$main_sha" && fail "unreadable checks were accepted"
# no origin/main
git update-ref -d refs/remotes/origin/main
gate "$main_sha" && fail "accepted without origin/main"

echo "test_release_gate: ok"
