#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
"""Every text the interface shows (T("..."), tr("...") in src/ui) has a German translation in po/de.po.

A text without one is shown in English inside the German interface. The check reads the string literals that are
passed to T() or tr() (and to fail() in the New function model, whose messages the dialog shows; adjacent literals are joined) and fails for those without a non-empty msgstr.
A literal that is meant to stay as it is (a key name) is listed in KEEP. The names of keys have their own context ("key",
TK(), msgctxt "key"): they do not count here, tests/test_ui_key_names.cpp checks that every key has one.
Usage: scripts/check-po-ui.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEEP = {"F1"}
LIT = r'"(?:[^"\\]|\\.)*"'


def unescape(s):
    s = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), s)
    return s.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def joined(block):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block))


def main():
    po = (ROOT / "po" / "de.po").read_text(encoding="utf-8")
    done = set()
    for m in re.finditer(r"^(msgctxt %s\n)?msgid ((?:%s\s*)+)\nmsgstr ((?:%s\s*)+)" % (LIT, LIT, LIT), po, re.M):
        if m.group(1):
            continue  # another context (the names of keys): not the one of T()
        if joined(m.group(3)):
            done.add(unescape(joined(m.group(2))))
    files = sorted((ROOT / "src" / "ui").rglob("*.cpp")) + sorted((ROOT / "src" / "ui").rglob("*.hpp"))
    bad = []
    for f in files:
        text = f.read_text(encoding="utf-8")
        for m in re.finditer(r"\b(?:tr|T%s)\(\s*((?:%s\s*)+)" % ("|fail" if f.name == "new_function_model.hpp" else "", LIT), text):
            lit = unescape(joined(m.group(1)))
            if len(lit) > 1 and lit not in KEEP and lit not in done:
                bad.append("%s:%d: %s" % (f.relative_to(ROOT), text[: m.start()].count("\n") + 1, lit[:100]))
    if bad:
        print("\n".join(bad), file=sys.stderr)
        print("check-po-ui: %d interface texts have no German translation in po/de.po" % len(bad), file=sys.stderr)
        return 1
    print("check-po-ui: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
