#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Fails when a tracked source, test, script, CMake or workflow file carries no
# "SPDX-License-Identifier" line. Usage: scripts/check-spdx.sh   (run from anywhere in the repo)
#
# Checked (tracked files only):
#   src/ and tests/ (*.cpp *.hpp *.h *.c *.cc), scripts/* (shell scripts, pre-commit hook),
#   CMakeLists.txt, *.cmake, .github/**/*.yml and *.yaml, packaging/**/*.cmake.
#
# Deliberately NOT checked (no or non-comment-capable format, or not code):
#   LICENSE, THIRD-PARTY-NOTICES.md and other prose (docs/ and *.md mostly carry the
#   identifier in an HTML comment, but prose is not gated here), po/* (translation catalogs;
#   the generated .pot and LINGUAS are tool-owned), devices/*.conf and examples/ (data
#   files, validated by the tests instead), systemd/*.in,
#   packaging/*.rules, icons/*.svg, .gitignore, .github/CODEOWNERS and issue/PR templates,
#   .github/rulesets/*.json (JSON has no comments).
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"

fail=0
list=$(git ls-files)
for f in $list; do
  case "$f" in
    src/*.cpp|src/*.hpp|src/*.h|src/*.c|src/*.cc) ;;   # '*' also matches '/', so subdirs are covered
    tests/*.cpp|tests/*.hpp|tests/*.h) ;;
    scripts/*) ;;
    CMakeLists.txt|*.cmake) ;;
    .github/*.yml|.github/*.yaml) ;;
    *) continue ;;
  esac
  [ -f "$f" ] || continue
  if ! grep -q 'SPDX-License-Identifier:' "$f"; then
    echo "missing SPDX-License-Identifier: $f" >&2
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "check-spdx: add '# SPDX-License-Identifier: GPL-3.0-or-later' (or the // form) to the files above" >&2
  exit 1
fi
echo "check-spdx: ok"
