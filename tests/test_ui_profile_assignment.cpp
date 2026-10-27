// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The effective assignment of a profile as the profile page and the Overview show it: the six motions of
// the effective joystick mode and the buttons (joystick mode first, then the profile's own, then inherited),
// each with a short word where it comes from.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui_fixture.hpp"
#include "testutil.hpp"
#include "ui/model/profile_assignment_model.hpp"

using namespace spn;
using namespace spn::ui;

int main() {
    useFreshDefaultOf("linux");  // not the host system
    IdentityStrings id;
    ModeSet modes = loadModes("/nonexistent-dir-for-test", nullptr);
    ModeDef mine = *modes.find("timeline");
    mine.id = "mine";
    mine.label = "Mine";
    mine.builtin = false;
    mine.buttons.push_back({3, ButtonActionType::Nav, Key::Space, "", {}});
    modes.modes.push_back(mine);

    ProfileSet ps;
    ps.sections[0].kv = {{"BUTTON_1", "key:space"}};
    const size_t viewer = ps.findOrAdd(Section::APP, "viewer");
    ps.sections[viewer].kv = {{"MODE", "mine"}, {"BUTTON_2", "rightclick"}, {"BUTTON_3", "click"}};
    const size_t plain = ps.findOrAdd(Section::APP, "plain");
    ps.sections[plain].kv = {{"BUTTON_4", "middleclick"}};
    SettingsModel m;
    m.setProfiles(ps, "");
    m.setModes(modes, "");

    // Global: normal mode, its own button
    ProfileAssignment g = profileAssignment(m, 0, id);
    assert(g.modeId == "normal" && g.motions.size() == 6 && g.motions[0].chip == "Scroll");
    assert(g.motions[0].source == "from the joystick mode");
    assert(g.buttons.size() == 1 && g.buttons[0].text == "1 Space" && g.buttons[0].source == "from this profile");

    // a profile with its own mode: motions from the mode, the mode's button wins over the profile's BUTTON_3,
    // BUTTON_2 is the profile's, BUTTON_1 is inherited from Global
    ProfileAssignment v = profileAssignment(m, viewer, id);
    assert(v.modeId == "mine" && v.modeLabel == "Mine");
    assert(v.motions.size() == 6 && v.motions[0].chip == "Off" && v.motions[2].chip == "Keys");
    for (const MotionRow &r : v.motions) assert(r.source == "from the joystick mode");
    assert(v.buttons.size() == 3);
    assert(v.buttons[0].text == "1 Space" && v.buttons[0].source == "inherited");
    assert(v.buttons[1].text == "2 Right click" && v.buttons[1].source == "from this profile");
    // BUTTON_3 of the program profile wins over the mode's own binding of button 3 (the program has the last word)
    assert(v.buttons[2].text == "3 Click" && v.buttons[2].source == "from this profile");

    // a profile without MODE inherits the joystick mode
    ProfileAssignment p = profileAssignment(m, plain, id);
    assert(p.modeId == "normal" && p.motions[0].source == "inherited");
    assert(p.buttons.size() == 2 && p.buttons[0].source == "inherited" && p.buttons[1].text == "4 Middle click" &&
           p.buttons[1].source == "from this profile");

    // a profile that inherits a global mode
    ps.sections[0].kv.push_back({"MODE", "timeline"});
    m.setProfiles(ps, "");
    m.setModes(modes, "");
    ProfileAssignment q = profileAssignment(m, plain, id);
    assert(q.modeId == "timeline" && q.motions[0].chip == "Off" && q.motions[0].source == "inherited");
    std::puts("ok");
    return 0;
}
