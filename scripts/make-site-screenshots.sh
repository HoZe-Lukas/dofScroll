#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Rebuilds the website screenshots in site/img/ (settings window and Overview, light and dark, English
# in site/img/ and German in site/img/de/). Run it after changes to the user interface.
#
# The pictures are rendered by `dofscroll-ui --screenshots` without hardware: a demo configuration
# (programs firefox, blender, gimp, okular; devices SpaceMouse Pro and Compact) and a status file as
# the daemon writes it (SpaceMouse Pro connected, joystick mode normal, running). Everything lives in
# private temporary directories; neither your configuration nor a running daemon is touched. The German
# variant uses a temporary ui.conf with `language = de`. The PNGs are palette-quantized afterwards
# (scripts/optimize-png.py).
#
# Usage: scripts/make-site-screenshots.sh [build-dir]       (default: build-ui, built with -DDOFSCROLL_UI=ON)
#   Qt libraries outside the system path: set QT_LIB_DIR=<qt>/lib.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build=${1:-$root/build-ui}
ui=$build/dofscroll-ui
[ -x "$ui" ] || { echo "make-site-screenshots: $ui not found; build it: cmake -S . -B build-ui -DDOFSCROLL_UI=ON && cmake --build build-ui --target dofscroll-ui" >&2; exit 1; }
py=$(command -v python3 || command -v python || true)
[ -n "$py" ] || { echo "make-site-screenshots: python3 is required" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
[ -z "${QT_LIB_DIR:-}" ] || export LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

for lang in en de; do
    cfg=$tmp/config-$lang
    mkdir -p "$cfg/dofscroll" "$tmp/shots-$lang"
    printf '# dofscroll user interface settings\nlanguage = %s\ntheme = system\n' "$lang" > "$cfg/dofscroll/ui.conf"
    XDG_CONFIG_HOME=$cfg QT_QPA_PLATFORM=offscreen "$ui" --screenshots "$tmp/shots-$lang" > /dev/null 2>&1
    if [ "$lang" = en ]; then dest=$root/site/img; else dest=$root/site/img/de; fi
    mkdir -p "$dest"
    for theme in light dark; do
        cp "$tmp/shots-$lang/site-profiles-$theme.png" "$dest/ui-window-$theme.png"
        cp "$tmp/shots-$lang/site-overview-$theme.png" "$dest/ui-overview-$theme.png"
    done
    "$py" "$here/optimize-png.py" "$dest"/ui-window-*.png "$dest"/ui-overview-*.png
done
echo "make-site-screenshots: done; run scripts/check-site.sh"
