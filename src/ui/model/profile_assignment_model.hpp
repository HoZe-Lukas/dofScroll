// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What a profile really does (toolkit-free): the six motions of the cap and the buttons, EFFECTIVE for the
// profile, with a short word for where each value comes from. The motions come from the effective joystick
// mode (with its mix overrides and the profile's mixes); the buttons are the bindings of the joystick mode
// (they win) plus the profile's own BUTTON_<n> (and the ones it inherits). Shown on the General tab of the
// profile page and, as a compact strip, on the Overview cards.
//
//   profileAssignment(settings, section, strings)
//     motions: six rows (motion_summary_model.hpp), each with source "from the joystick mode" or "inherited"
//     buttons: chips of the bound buttons; source "from the joystick mode", "from this profile" or "inherited"
#pragma once
#include <string>
#include <vector>

#include "ui/model/button_chip_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct ProfileAssignment {
    std::string modeId, modeLabel;
    std::vector<MotionRow> motions;
    std::vector<ButtonChip> buttons;
};

inline ProfileAssignment profileAssignment(const SettingsModel &m, size_t section, const StringProvider &s) {
    ProfileAssignment a;
    a.modeId = m.effectiveMode(section);
    const ModeDef *def = m.modeDef(a.modeId);
    if (!def) def = m.modeDef("normal");
    ModeDef normal;  // all channels default, no buttons (a model without modes loaded)
    if (!def) def = &normal;
    a.modeLabel = def->label.empty() ? def->id : def->label;
    const Settings settings = m.effectiveWithMode(section);
    // the profile cannot set channel actions: they are the joystick mode's, chosen here or inherited
    const bool modeHere = section == 0 || m.isOverridden(section, "MODE");
    // the actions the profile really runs: the joystick mode's with the channel actions of the profiles merged in
    ModeDef run = *def;
    {
        const ChannelActions ca = effectiveChannelActions(*def, settings);
        run.v = ca.v;
        run.h = ca.h;
        run.twist = ca.twist;
        run.vSplit = ca.vSplit;
        run.hSplit = ca.hSplit;
    }
    a.motions = motionSummary(run, settings, s);
    for (MotionRow &r : a.motions) {
        r.source = modeHere ? s.tr("from the joystick mode") : s.tr("inherited");
        // a motion the profile clears says so, one it gives another action says that; never the action it would have
        const MotionEdit e = motionEdit(m, section, r.id, s);
        if (e.editable && e.state == MotionState::Cleared) r.source = s.tr("cleared in this profile");
        else if (e.editable && (m.isOverridden(section, e.key) || m.isOverridden(section, e.prefix.empty() ? "" : r.id[0] == 'v' ? "V_ACTION" : "H_ACTION")))
            r.source = s.tr("set in this profile");
    }
    // buttons 1..32: a binding of the joystick mode wins over BUTTON_<n> of [global] and [device:*]; BUTTON_<n> of a
    // program profile wins over the mode (the program has the last word)
    const bool isApp = m.sections()[section].kind == Section::APP;
    for (int n = 1; n <= int(kMaxButtons); ++n) {
        const ButtonBinding *bound = nullptr;
        for (const ButtonBinding &b : def->buttons)
            if (b.button == n && b.type != ButtonActionType::Unbound) bound = &b;
        if (bound && !(isApp && m.isOverridden(section, "BUTTON_" + std::to_string(n)))) {
            a.buttons.push_back(buttonChip(n, modelist::buttonActionText(*bound), s.tr("from the joystick mode"), s));
            continue;
        }
        const auto it = settings.buttons.find(n);
        const bool own = m.isOverridden(section, "BUTTON_" + std::to_string(n));
        if (own && section > 0 && (it == settings.buttons.end() || it->second == "none")) {
            // explicitly nothing in this profile (BUTTON_<n> = none): cleared, even when Global or the device binds it
            ButtonChip c = buttonChip(n, "none", s.tr("cleared in this profile"), s);
            c.action = s.tr("Cleared");
            c.text = std::to_string(n) + " " + c.action;
            c.tooltip = format(s.tr("Button %1: cleared in this profile, nothing happens"), {std::to_string(n)});
            a.buttons.push_back(c);
            continue;
        }
        if (it == settings.buttons.end() || it->second.empty() || it->second == "none") continue;
        a.buttons.push_back(buttonChip(n, it->second, own ? s.tr("from this profile") : s.tr("inherited"), s));
    }
    return a;
}

// What the profile editor says about a button that the profile's joystick mode binds itself: "" when it does not.
// Precedence: the mode beats [global] and [device:*], a program profile beats the mode.
inline std::string buttonModeNote(const SettingsModel &m, size_t section, int number, const StringProvider &s) {
    const ModeDef *def = m.modeDef(m.effectiveMode(section));
    if (!def) return std::string();
    const ButtonBinding *bound = nullptr;
    for (const ButtonBinding &b : def->buttons)
        if (b.button == number && b.type != ButtonActionType::Unbound) bound = &b;
    if (!bound) return std::string();
    const std::string mode = def->label.empty() ? def->id : def->label;
    const std::string words = buttonActionWords(modelist::buttonActionText(*bound), s);
    if (m.sections()[section].kind == Section::APP) {
        if (m.isOverridden(section, "BUTTON_" + std::to_string(number)))
            return format(s.tr("This profile's binding overrides the one of the joystick mode \"%1\" (%2)."), {mode, words});
        return format(s.tr("The joystick mode \"%1\" binds this button (%2). A binding set here overrides it."), {mode, words});
    }
    return format(s.tr("The joystick mode \"%1\" binds this button (%2) and wins over this profile. Only a program profile can override the binding of a joystick mode."),
                  {mode, words});
}

}  // namespace spn::ui
