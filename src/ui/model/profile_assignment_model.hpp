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
    a.motions = motionSummary(*def, settings, s);
    for (MotionRow &r : a.motions) r.source = modeHere ? s.tr("from the joystick mode") : s.tr("inherited");
    // buttons 1..32: a binding of the joystick mode wins over BUTTON_<n> of the profile
    for (int n = 1; n <= int(kMaxButtons); ++n) {
        const ButtonBinding *bound = nullptr;
        for (const ButtonBinding &b : def->buttons)
            if (b.button == n && b.type != ButtonActionType::Unbound) bound = &b;
        if (bound) {
            a.buttons.push_back(buttonChip(n, modelist::buttonActionText(*bound), s.tr("from the joystick mode"), s));
            continue;
        }
        const auto it = settings.buttons.find(n);
        if (it == settings.buttons.end() || it->second.empty() || it->second == "none") continue;
        const bool own = m.isOverridden(section, "BUTTON_" + std::to_string(n));
        a.buttons.push_back(buttonChip(n, it->second, own ? s.tr("from this profile") : s.tr("inherited"), s));
    }
    return a;
}

}  // namespace spn::ui
