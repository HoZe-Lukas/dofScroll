// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Counts and orderings of the built-in mode library for tests, derived from the library itself.
// The embedded set is whatever examples/modes/*.conf and examples/buttons/*.conf contain at
// configure time, so no test may hard-code how many built-ins exist or which ids they have:
// adding a mode file must never need a test change.
#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "builtin_library.hpp"
#include "button_modes.hpp"
#include "modes.hpp"

namespace tu {

// Number of entries of a library list (as builtinModeFiles() returns).
inline size_t fileCount(const spn::BuiltinFile *list) {
    size_t n = 0;
    for (; list && list->id; ++list) n++;
    return n;
}

// normal, off and timeline are defined in code (a file of the same id does not add a mode).
inline bool isCodeDefinedMode(const std::string &id) { return id == "normal" || id == "off" || id == "timeline"; }
inline size_t codeDefinedModeCount() { return 3; }

// Joystick modes that exist without any user file, for the given library (nullptr = embedded).
inline size_t builtinModeCount(const spn::BuiltinFile *library = nullptr) {
    if (!library) library = spn::builtinModeFiles();
    size_t n = codeDefinedModeCount();
    for (const spn::BuiltinFile *f = library; f && f->id; ++f)
        if (!isCodeDefinedMode(f->id)) n++;
    return n;
}

inline size_t builtinButtonModeCount(const spn::BuiltinFile *library = nullptr) {
    return fileCount(library ? library : spn::builtinButtonModeFiles());
}

// All mode ids of a loaded set, sorted as the daemon's mode:next / mode:prev cycle sorts them.
inline std::vector<std::string> sortedIds(const spn::ModeSet &ms) {
    std::vector<std::string> ids;
    for (const spn::ModeDef &m : ms.modes) ids.push_back(m.id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Ids of the embedded joystick example files (no code-defined mode), sorted.
inline std::vector<std::string> embeddedExampleIds() {
    std::vector<std::string> ids;
    for (const spn::BuiltinFile *f = spn::builtinModeFiles(); f && f->id; ++f)
        if (!isCodeDefinedMode(f->id)) ids.push_back(f->id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace tu
