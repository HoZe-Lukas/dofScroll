// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The built-in mode library: the example files examples/modes/*.conf (joystick modes) and
// examples/buttons/*.conf (button modes), embedded into the program at build time
// (cmake/BuiltinLibrary.cmake). loadModes() and loadButtonModes() parse them with the normal file
// parsers, so a built-in behaves exactly like an installed file. Nothing is ever written to disk.
#pragma once

#include <string>

namespace spn {

struct BuiltinFile {
    const char *id;    // file name without ".conf"; nullptr ends the list
    const char *text;  // NUL-terminated file content
};

// Lists ending with an entry whose id is nullptr, sorted by id.
const BuiltinFile *builtinModeFiles();
const BuiltinFile *builtinButtonModeFiles();
// Joystick mode files that earlier versions copied into the user's modes folder on first start
// (examples/legacy/modes, ids as the file names then). A user file with exactly this text is the
// shipped example, so the loaders do not list it as the user's own file.
const BuiltinFile *legacyModeFiles();

// True if text is byte-identical to the embedded text with this id in the list.
inline bool matchesBuiltinText(const BuiltinFile *list, const std::string &id, const std::string &text) {
    for (; list && list->id; ++list)
        if (id == list->id) return text == list->text;
    return false;
}

}  // namespace spn
