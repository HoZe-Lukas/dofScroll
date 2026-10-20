#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# The interface knows two kinds of modes and calls them "joystick mode" and "button mode" (issue #81,
# src/ui/model/ui_terms.hpp). This check fails when a translatable UI string (T("..."), tr("..."), N_("..."))
# says plain "mode" or "modes" without one of those words. The one exception is the sidebar category
# "Modes"; config keys and file names are written in code spans and never appear as UI text.
set -eu
cd "$(dirname "$0")/.."
exec python3 - <<'PY'
import glob, re, sys
call = re.compile(r'(?:\bT|\btr|\bN_|\bQT_TR_NOOP)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
bad = 0
for f in sorted(glob.glob('src/**/*.[ch]pp', recursive=True)):
    text = open(f, encoding='utf-8').read()
    for m in call.finditer(text):
        s = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
        if s == 'Modes':
            continue
        stripped = re.sub(r'(?i)\b(joystick|button|navigation) modes?\b', '', s)
        stripped = re.sub(r'[\w./-]*modes?\.md|modes/', '', stripped)  # file names
        if re.search(r'(?i)\bmodes?\b', stripped):
            print('%s:%d: plain "mode" in a UI string: %s' % (f, text[:m.start()].count('\n') + 1, s[:100]))
            bad += 1
if bad:
    print('check-terms: %d string(s); say "joystick mode" or "button mode" (docs/ui.md, section Terms)' % bad)
    sys.exit(1)
print('check-terms: ok')
PY
