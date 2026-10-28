#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Do the libraries that the Linux packages bundle have a newer security fix? (issue #265)
#   scripts/check-bundled-advisories.sh <bundled-libraries.txt> <updateinfo.txt>
# bundled-libraries.txt is the inventory of a built package (scripts/bundle-qt-linux.sh: "library | source package |
# version | license | upstream", RPM names with version-release). updateinfo.txt is the output of
# `dnf updateinfo list --security --all` of the build distribution (Rocky Linux 9):
#   RLSA-2025:1234 Important/Sec. freetype-2.10.4-10.el9_5.x86_64
# For every source package of the inventory, an advisory for the same package name whose version-release sorts
# higher than the bundled one (sort -V, an approximation of rpm's comparison) is printed. Exit 0 when there is none,
# 3 when there is at least one, 2 on a usage error. Packages from qt.io (Qt itself) are watched by qt-update-check.yml;
# Debian packages are not covered (the DEB inventory lists dpkg versions). Used by bundled-libs-advisories.yml.
set -eu
inv="${1:-}"
upd="${2:-}"
[ -f "$inv" ] && [ -f "$upd" ] || { echo "usage: check-bundled-advisories.sh <bundled-libraries.txt> <updateinfo.txt>" >&2; exit 2; }

# unique "name version" pairs of the inventory: RPM-owned libraries only
pairs="$(grep -v '^#' "$inv" | awk -F ' [|] ' 'NF >= 3 && $2 != "" && $2 != "qt.io" && $3 != "" { print $2 " " $3 }' | LC_ALL=C sort -u)"
[ -n "$pairs" ] || { echo "check-bundled-advisories: no RPM-owned library in $inv" >&2; exit 0; }

printf '%s\n' "$pairs" | while read -r name bundled; do
  # advisory lines: "<id> <severity> <name>-<version>-<release>.<arch>"
  awk -v n="$name" '
    NF >= 3 {
      nvra = $3
      sub(/\.[A-Za-z0-9_]+$/, "", nvra)          # drop the architecture
      m = split(nvra, p, "-")
      if (m < 3) next
      rel = p[m]; ver = p[m-1]
      nm = p[1]; for (i = 2; i <= m - 2; i++) nm = nm "-" p[i]
      if (nm == n) print $1 "\t" $2 "\t" ver "-" rel
    }' "$upd" | while IFS="$(printf '\t')" read -r id sev fixed; do
    newest="$(printf '%s\n%s\n' "$bundled" "$fixed" | LC_ALL=C sort -V | tail -n 1)"
    if [ "$fixed" != "$bundled" ] && [ "$newest" = "$fixed" ]; then
      echo "$name: bundled $bundled, $id ($sev) is fixed in $fixed"
    fi
  done
done > "${TMPDIR:-/tmp}/bundled-advisories.$$"
status=0
if [ -s "${TMPDIR:-/tmp}/bundled-advisories.$$" ]; then
  LC_ALL=C sort -u "${TMPDIR:-/tmp}/bundled-advisories.$$"
  status=3
fi
rm -f "${TMPDIR:-/tmp}/bundled-advisories.$$"
exit "$status"
