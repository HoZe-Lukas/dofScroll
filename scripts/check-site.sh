#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Validates the website in site/ (links, images, titles and descriptions, no external resources,
# Content-Security-Policy hash, English/German parity, WCAG contrast, size budget). The work is
# done by scripts/check-site.py; this wrapper only finds Python 3.
#
# Usage: scripts/check-site.sh [site-dir]   (run from anywhere)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
py=$(command -v python3 || command -v python || true)
[ -n "$py" ] || { echo "check-site: python3 is required" >&2; exit 1; }
exec "$py" "$here/check-site.py" "$@"
