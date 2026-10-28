#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Test of scripts/check-bundled-advisories.sh with a fixture inventory and `dnf updateinfo` output.
# Usage: sh tests/test_bundled_advisories.sh <repo dir>
set -u
SRC=${1:?usage: test_bundled_advisories.sh <repo dir>}
T=$(mktemp -d) || exit 1
[ -n "$T" ] && [ -d "$T" ] || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "test_bundled_advisories: FAIL: $*" >&2; exit 1; }
S="$SRC/scripts/check-bundled-advisories.sh"

cat > "$T/inv" <<'EOS'
# library | source package | version | license | upstream
libQt6Core.so.6 | qt.io | 6.9.3 | LGPL-3.0-only | https://www.qt.io/download-open-source
libfreetype.so.6 | freetype | 2.10.4-10.el9 | FTL | https://freetype.org
libpng16.so.16 | libpng | 1.6.37-12.el9 | zlib | http://www.libpng.org
libxml2.so.2 | libxml2 | 2.9.13-6.el9_4 | MIT | https://gitlab.gnome.org/GNOME/libxml2
libglib-2.0.so.0 | glib2 | 2.68.4-14.el9 | LGPLv2+ | https://wiki.gnome.org/Projects/GLib
libgobject-2.0.so.0 | glib2 | 2.68.4-14.el9 | LGPLv2+ | https://wiki.gnome.org/Projects/GLib
libX11.so.6 | libX11 | 1.7.0-9.el9 | MIT | https://www.x.org
EOS
cat > "$T/upd" <<'EOS'
Last metadata expiration check: 0:00:01 ago.
RLSA-2025:1111 Important/Sec. freetype-2.10.4-11.el9_5.x86_64
RLSA-2024:2222 Moderate/Sec. freetype-2.10.4-9.el9.x86_64
RLSA-2025:3333 Critical/Sec. libxml2-2.9.13-6.el9_4.x86_64
RLSA-2025:4444 Important/Sec. libxml2-devel-2.9.13-9.el9_5.x86_64
RLSA-2025:5555 Moderate/Sec. glib2-2.68.4-16.el9_5.x86_64
RLSA-2025:5555 Moderate/Sec. glib2-2.68.4-16.el9_5.i686
RLSA-2025:6666 Low/Sec. unrelated-pkg-1.0-1.el9.noarch
RLSA-2025:7777 Important/Sec. libX11-1.7.0-9.el9.x86_64
EOS
out=$(sh "$S" "$T/inv" "$T/upd"); rc=$?
[ "$rc" = 3 ] || fail "findings expected (exit 3), got $rc: $out"
printf '%s\n' "$out" | grep -q '^freetype: bundled 2.10.4-10.el9, RLSA-2025:1111 (Important/Sec.) is fixed in 2.10.4-11.el9_5$' || fail "freetype advisory missing: $out"
printf '%s\n' "$out" | grep -q '^glib2: bundled 2.68.4-14.el9, RLSA-2025:5555' || fail "glib2 advisory missing: $out"
[ "$(printf '%s\n' "$out" | grep -c '^glib2:')" = 1 ] || fail "glib2 reported twice (two libraries of one package, two architectures)"
printf '%s\n' "$out" | grep -q 'RLSA-2024:2222' && fail "an older advisory was reported"
printf '%s\n' "$out" | grep -q 'libxml2:' && fail "same version as bundled, and the -devel package, were reported: $out"
printf '%s\n' "$out" | grep -q 'unrelated' && fail "a package that is not bundled was reported"
printf '%s\n' "$out" | grep -q 'libX11' && fail "the bundled version itself was reported"
printf '%s\n' "$out" | grep -q 'Qt\|qt.io' && fail "Qt is not covered here"

# no findings: exit 0 and no output
printf 'RLSA-2020:1 Low/Sec. other-1.0-1.el9.x86_64\n' > "$T/upd2"
out=$(sh "$S" "$T/inv" "$T/upd2"); rc=$?
[ "$rc" = 0 ] && [ -z "$out" ] || fail "no findings expected, got $rc: $out"
# usage errors
sh "$S" "$T/none" "$T/upd2" >/dev/null 2>&1; [ "$?" = 2 ] || fail "missing inventory: exit 2 expected"
echo "test_bundled_advisories: ok"
