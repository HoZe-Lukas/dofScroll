// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key chooser data for the button binding editor (toolkit-free): the names from keys.hpp, in
// groups, so the view can offer a grouped list. Every name in keys.hpp is listed exactly once.
#pragma once
#include <string>
#include <vector>

#include "keys.hpp"

namespace spn::ui {

struct KeyGroup {
    std::string id;     // "modifiers", "navigation", "editing", "symbols", "function", "letters", "digits"
    std::string title;  // English source string (translate in the view)
    std::vector<std::string> keys;
};

// All key names in keys.hpp order.
inline std::vector<std::string> allKeyNames() {
    std::vector<std::string> out;
    for (size_t i = 0; i < kKeyCount; ++i) out.push_back(keyName(static_cast<Key>(i)));
    return out;
}

inline std::string keyGroupIdOf(const std::string &name) {
    if (name == "ctrl" || name == "shift" || name == "alt" || name == "super") return "modifiers";
    if (name == "up" || name == "down" || name == "left" || name == "right" || name == "home" ||
        name == "end" || name == "pageup" || name == "pagedown" || name == "tab")
        return "navigation";
    if (name == "space" || name == "enter" || name == "esc" || name == "backspace" ||
        name == "delete" || name == "insert")
        return "editing";
    if (name == "minus" || name == "plus" || name == "kp_plus" || name == "kp_minus") return "symbols";
    if (name.size() >= 2 && name[0] == 'f' && name[1] >= '0' && name[1] <= '9') return "function";
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') return "digits";
    return "letters";
}

// Grouped key list (empty groups omitted). Group order is fixed, keys keep keys.hpp order.
inline std::vector<KeyGroup> keyGroups() {
    std::vector<KeyGroup> g = {{"modifiers", "Modifiers", {}},    {"navigation", "Navigation", {}},
                               {"editing", "Editing", {}},        {"symbols", "Symbols", {}},
                               {"function", "Function keys", {}}, {"letters", "Letters", {}},
                               {"digits", "Digits", {}}};
    for (const std::string &n : allKeyNames()) {
        const std::string id = keyGroupIdOf(n);
        for (KeyGroup &x : g)
            if (x.id == id) x.keys.push_back(n);
    }
    std::vector<KeyGroup> out;
    for (KeyGroup &x : g)
        if (!x.keys.empty()) out.push_back(std::move(x));
    return out;
}

}  // namespace spn::ui
