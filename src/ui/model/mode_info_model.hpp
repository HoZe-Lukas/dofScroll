// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the interface says about a joystick mode or a button mode when the user wants to know what it
// is (toolkit-free): the short description for tooltips of drop-downs, list rows and the tray menu,
// and the longer text of the "i" info popover (name, where it comes from, description, what the
// mode does per channel in plain words). A mode without a description says "No description"; the
// editors always offer the description field.
//
//   joystickModeTooltip(mode, strings)            // description or "No description"
//   buttonModeTooltip(buttonMode, strings)
//   joystickModeInfo(mode, "Built in", strings)   // the multi-line text of the popover
//   channelActionWords(action, strings)           // "scrolls vertically", "taps Left / Right ..."
#pragma once
#include <string>
#include <vector>

#include "button_modes.hpp"
#include "mode_list.hpp"
#include "modes.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

inline std::string noDescriptionText(const StringProvider &s) { return s.tr("No description"); }

// The description of a joystick mode, or "No description".
inline std::string joystickModeTooltip(const ModeDef &m, const StringProvider &s) {
    return m.description.empty() ? noDescriptionText(s) : m.description;
}

// The description of a button mode, or "No description".
inline std::string buttonModeTooltip(const ButtonMode &b, const StringProvider &s) {
    return b.description.empty() ? noDescriptionText(s) : b.description;
}

// What one channel does with this action, in plain words.
inline std::string channelActionWords(const ChannelAction &a, const StringProvider &s) {
    switch (a.type) {
        case ChannelActionType::Default: return s.tr("works as set in the profile (scroll and zoom as configured)");
        case ChannelActionType::Off: return s.tr("does nothing");
        case ChannelActionType::WheelV: return s.tr("scrolls vertically");
        case ChannelActionType::WheelH: return s.tr("scrolls horizontally");
        case ChannelActionType::Zoom: return s.tr("zooms (Ctrl + wheel)");
        case ChannelActionType::PointerX: return s.tr("moves the mouse pointer left and right");
        case ChannelActionType::PointerY: return s.tr("moves the mouse pointer up and down");
        case ChannelActionType::Keys:
            return format(s.tr("taps %1 in one direction and %2 in the other, faster the further you push"),
                          {modelist::negChordText(a), modelist::posChordText(a)});
        case ChannelActionType::Step:
            return format(s.tr("taps %1 or %2 once per push"),
                          {modelist::negChordText(a), modelist::posChordText(a)});
        case ChannelActionType::Hold:
            return format(s.tr("holds %1 or %2 while pushed"),
                          {modelist::negChordText(a), modelist::posChordText(a)});
        case ChannelActionType::Macro: return s.tr("runs a macro once per push");
    }
    return std::string();
}

// The channel lines of a joystick mode ("Vertical push: scrolls vertically", ...). A split channel
// gets one line per part ("Vertical, push part: ...", "Vertical, tilt part: ..."); a part without its
// own action follows the channel's action.
inline std::vector<std::string> joystickModeChannelLines(const ModeDef &m, const StringProvider &s) {
    std::vector<std::string> out;
    if (m.vSplit.active()) {
        out.push_back(format(s.tr("Vertical, push part: %1"), {channelActionWords(m.vSplit.pushAction(m.v), s)}));
        out.push_back(format(s.tr("Vertical, tilt part: %1"), {channelActionWords(m.vSplit.tiltAction(m.v), s)}));
    } else {
        out.push_back(format(s.tr("Vertical push: %1"), {channelActionWords(m.v, s)}));
    }
    if (m.hSplit.active()) {
        out.push_back(format(s.tr("Horizontal, push part: %1"), {channelActionWords(m.hSplit.pushAction(m.h), s)}));
        out.push_back(format(s.tr("Horizontal, tilt part: %1"), {channelActionWords(m.hSplit.tiltAction(m.h), s)}));
    } else {
        out.push_back(format(s.tr("Horizontal push: %1"), {channelActionWords(m.h, s)}));
    }
    out.push_back(format(s.tr("Twist: %1"), {channelActionWords(m.twist, s)}));
    return out;
}

// The text of the "i" popover: name (and id), where it comes from, the description, what the channels
// do (the six motions of the cap, see motion_summary_model.hpp) and how many buttons the joystick mode binds. originText: "Built in" / "Your file" ("" = left out).
inline std::string joystickModeInfo(const ModeDef &m, const std::string &originText, const StringProvider &s) {
    const std::string name = m.label.empty() ? m.id : m.label;
    std::string t = name == m.id ? name : name + " (" + m.id + ")";
    if (!originText.empty()) t += "\n" + originText;
    t += "\n\n" + joystickModeTooltip(m, s) + "\n\n" + s.tr("What each motion does:") + "\n" +
         motionSummaryText(motionSummary(m, s));
    const std::vector<ButtonChip> bound = modeButtonChips(m, s);
    if (!bound.empty()) t += "\n\n" + format(s.tr("Buttons: %1"), {buttonChipsText(bound)});
    return t;
}

// The same for a button mode: name, description and the action.
inline std::string buttonModeInfo(const ButtonMode &b, const std::string &originText, const StringProvider &s) {
    const std::string name = b.label.empty() ? b.id : b.label;
    std::string t = name == b.id ? name : name + " (" + b.id + ")";
    if (!originText.empty()) t += "\n" + originText;
    t += "\n\n" + buttonModeTooltip(b, s);
    t += "\n\n" + format(s.tr("Action: %1"), {b.action});
    return t;
}

}  // namespace spn::ui
