// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The text of a chord ("ctrl+shift+pageup"): split into modifiers and key, and joined again (modifiers in the fixed order ctrl, shift,
// alt, super). Shared by the chord pickers (key_combo.hpp) and the "Press a key..." button (picker_capture.hpp).
#pragma once
#include <algorithm>
#include <string>
#include <vector>

// "ctrl+shift+pageup" -> {ctrl, shift} + "pageup"; a lone modifier is the key itself.
inline void splitChord(const std::string &chord, std::vector<std::string> *mods, std::string *key) {
    mods->clear();
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t p; (p = chord.find('+', start)) != std::string::npos; start = p + 1) parts.push_back(chord.substr(start, p - start));
    parts.push_back(chord.substr(start));
    *key = parts.back();
    parts.pop_back();
    *mods = parts;
}

inline constexpr const char *kModNames[4] = {"ctrl", "shift", "alt", "super"};

inline std::string joinChord(const std::vector<std::string> &mods, const std::string &key) {
    std::string out;
    for (const char *m : kModNames)
        if (std::find(mods.begin(), mods.end(), m) != mods.end()) out += std::string(m) + "+";
    return out + key;
}

