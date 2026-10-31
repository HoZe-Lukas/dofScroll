// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What a button does, as a short chip (toolkit-free): the number and the action in a few words ("1 Space",
// "2 Right click", "15 Navigation"). The one description of a button that every view uses (Joystick modes page,
// profile page, Overview, the "i" popover, Button modes page), so that a button looks the same everywhere. An
// unassigned button is muted and says "Unassigned" in words. Long text (the config text of the action, where
// the value comes from) goes into the tooltip.
//
//   buttonActionWords("key:space", strings)            // "Space"
//   buttonChip(2, "rightclick", "from this profile", strings)
//   modeButtonChips(mode, strings)                      // the bound buttons of a joystick mode
#pragma once
#include <string>
#include <vector>

#include "keys.hpp"
#include "mode_list.hpp"
#include "modes.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct ButtonChip {
    int number = 1;       // 1-based
    std::string action;   // short words: "Space", "Right click", "Navigation", "Unassigned"
    std::string text;     // the chip text: "1 Space"
    std::string tooltip;  // "Button 1: Space (key:space), from this profile"
    std::string source;   // "", "from this profile", "from the joystick mode", "inherited"
    bool muted = false;   // unassigned
};

// The action (config text such as "key:space", "mode:next", "none") in a few words.
inline std::string buttonActionWords(const std::string &configText, const StringProvider &s) {
    ButtonActionType type = ButtonActionType::Unbound;
    Key key = Key::Space;
    ChordMods mods;
    std::string err, id;
    if (configText.empty() || !parseButtonAction(configText, &type, &key, &err, &id, &mods)) return s.tr("Unassigned");
    switch (type) {
        case ButtonActionType::Unbound: return s.tr("Unassigned");
        case ButtonActionType::Click: return s.tr("Click");
        case ButtonActionType::RightClick: return s.tr("Right click");
        case ButtonActionType::MiddleClick: return s.tr("Middle click");
        case ButtonActionType::Nav: return s.tr("Navigation");
        case ButtonActionType::Key: return motion_detail::keyWords(keyActionText(key, mods).substr(4), s);  // after "key:"
        case ButtonActionType::ToggleNav: return s.tr("Toggle navigation");
        case ButtonActionType::ModeSwitch: {
            bool forward = true;
            std::vector<std::string> ids;
            if (splitCycleTarget(id, &forward, &ids)) return forward ? s.tr("Next joystick mode") : s.tr("Previous joystick mode");
            return format(s.tr("Switch to %1"), {id});
        }
        case ButtonActionType::ToggleMode: return format(s.tr("Toggle: %1"), {id});
        case ButtonActionType::HoldMode: return format(s.tr("Hold: %1"), {id});
        case ButtonActionType::ButtonMode: return format(s.tr("Button mode: %1"), {id});
        case ButtonActionType::Macro: return s.tr("Macro");
    }
    return s.tr("Unassigned");
}

// One chip. `source` may be empty.
inline ButtonChip buttonChip(int number, const std::string &configText, const std::string &source, const StringProvider &s) {
    ButtonChip c;
    c.number = number;
    c.action = buttonActionWords(configText, s);
    c.muted = configText.empty() || configText == "none";
    c.text = std::to_string(number) + " " + c.action;
    c.source = source;
    c.tooltip = format(s.tr("Button %1: %2"), {std::to_string(number), c.action});
    if (!c.muted) c.tooltip += " (" + configText + ")";
    if (!source.empty()) c.tooltip += ", " + source;
    return c;
}

// The bound buttons of a joystick mode (unbound ones are left out), in file order.
inline std::vector<ButtonChip> modeButtonChips(const ModeDef &m, const StringProvider &s, const std::string &source = std::string()) {
    std::vector<ButtonChip> out;
    for (const ButtonBinding &b : m.buttons)
        if (b.type != ButtonActionType::Unbound) out.push_back(buttonChip(b.button, modelist::buttonActionText(b), source, s));
    return out;
}

// "1 Space, 2 Right click" for the popover and tooltips.
inline std::string buttonChipsText(const std::vector<ButtonChip> &chips, const char *separator = ", ") {
    std::string t;
    for (const ButtonChip &c : chips) {
        if (!t.empty()) t += separator;
        t += c.text;
    }
    return t;
}

}  // namespace spn::ui
