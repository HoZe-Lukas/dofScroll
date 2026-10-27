#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# READ-ONLY survey of the 3Dconnexion driver's per-user preferences on macOS (issue #184).
# Changes nothing: it only lists files and prints plists as text. Output goes to
# dofscroll-macos-driver-survey.txt in the current folder; attach that file to issue #184.
# Usage: sh scripts/macos-driver-survey.sh
# Procedure and what to do by hand: docs/windows-mac.md, section "macOS driver cleanup: survey".

out=dofscroll-macos-driver-survey.txt
prefs="$HOME/Library/Preferences"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script is for macOS only." >&2
  exit 1
fi

{
  echo "== macOS"
  sw_vers
  echo
  echo "== 3Dconnexion files (name, size, date)"
  ls -l "$prefs"/com.3dconnexion* 2>/dev/null
  ls -lR "$HOME/Library/Application Support/3Dconnexion" 2>/dev/null
  for f in "$prefs"/com.3dconnexion*.plist; do
    [ -f "$f" ] || continue
    echo
    echo "== $f"
    file "$f"
    # Print as text. A binary plist is converted on the fly; the file itself is not touched.
    plutil -p "$f" 2>&1
  done
} > "$out"

echo "Wrote $out. Read it before sharing: it lists the names of your programs."
