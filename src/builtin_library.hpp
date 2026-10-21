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
// (examples/legacy/modes: "<id>.conf", and "<id>.<n>.conf" for every further text of the same id; the id
// is the part before the first dot). A user file with exactly one of these texts is the shipped example,
// so the loaders do not list it as the user's own file. These are only for matching, never modes.
const BuiltinFile *legacyModeFiles();

// True if text is byte-identical to the embedded text of ANY entry with this id in the list (the legacy
// list holds several texts per id: the file the seeding copied changed between versions).
inline bool matchesBuiltinText(const BuiltinFile *list, const std::string &id, const std::string &text) {
    for (; list && list->id; ++list)
        if (id == list->id && text == list->text) return true;
    return false;
}

}  // namespace spn
