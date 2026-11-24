// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS clipboard (general pasteboard) for the macro step paste:"text", implemented in mac_clipboard.mm (Objective-C++).
// Plain C++ interface so mac_output.cpp stays C++17. Text only.
#pragma once
#include <string>

namespace spn {

// The text on the clipboard (UTF-8). false if there is none (empty, or something that is not text).
bool macClipboardGetText(std::string *out);
// Replaces the clipboard with the text. Returns the change count after the write (see macClipboardChangeCount), -1 on failure.
long macClipboardSetText(const std::string &utf8);
// Empties the clipboard. Returns the change count after it, -1 on failure.
long macClipboardClear();
// NSPasteboard changeCount: grows with every change of the clipboard, by any program.
long macClipboardChangeCount();

}  // namespace spn
