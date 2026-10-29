// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key chords: the modifier part of "ctrl+shift+a" and its parser. Shared by the action grammar (modes.hpp: keys:, step:,
// hold:, key:) and the macro steps (macro.hpp). Toolkit-free.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "keys.hpp"

namespace spn {

// Modifiers of a key chord (keys:ctrl+pageup,...), in the order written; at most kMaxChordMods.
constexpr size_t kMaxChordMods = 4;
struct ChordMods {
    Key k[kMaxChordMods] = {Key::Ctrl, Key::Ctrl, Key::Ctrl, Key::Ctrl};
    uint8_t n = 0;
    // Order does not matter for equality: one bit per modifier key.
    unsigned mask() const {
        unsigned m = 0;
        for (uint8_t i = 0; i < n; ++i)
            m |= k[i] == Key::Ctrl ? 1u : k[i] == Key::Shift ? 2u : k[i] == Key::Alt ? 4u : 8u;
        return m;
    }
    bool operator==(const ChordMods &o) const { return mask() == o.mask(); }
};

// <chord> = [<modifier>+]... <key>: modifiers (ctrl, shift, alt, super) at most once each, then exactly one non-modifier
// key. A single name (also a lone modifier) is a plain key. With allowModifiersOnly the last name may be a modifier too
// ("ctrl+shift": only modifiers, held together). false + *err otherwise.
bool parseChord(const std::string &text, Key *key, ChordMods *mods, std::string *err, bool allowModifiersOnly = false);

}  // namespace spn
