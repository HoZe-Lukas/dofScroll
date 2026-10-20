// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the interface says about a mode: every built-in joystick mode has a description, the tooltips of
// drop-downs and the tray carry it, an empty description says "No description", and the info text names
// what the channels do in plain words.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "testutil.hpp"
#include "ui/model/button_choice_model.hpp"
#include "ui/model/mode_info_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/tray_menu_model.hpp"

using namespace spn;
using namespace spn::ui;

int main() {
    IdentityStrings id;
    // every built-in joystick mode has a description (and a name)
    const ModeSet builtins = loadModes("/nonexistent-dir-for-test", nullptr);
    assert(builtins.modes.size() >= 3);
    for (const ModeDef &m : builtins.modes) {
        assert(m.builtin && !m.description.empty() && !m.label.empty());
        assert(joystickModeTooltip(m, id) == m.description);
    }
    // a mode without a description says so
    ModeDef plain;
    plain.id = "mine";
    plain.label = "Mine";
    assert(joystickModeTooltip(plain, id) == "No description");
    ButtonMode bm;
    bm.id = "latch";
    bm.action = "toggle:nav";
    assert(buttonModeTooltip(bm, id) == "No description");
    bm.description = "Press once to switch on";
    assert(buttonModeTooltip(bm, id) == "Press once to switch on");

    // channels in plain words
    const ModeDef *timeline = builtins.find("timeline");
    assert(timeline);
    const std::vector<std::string> lines = joystickModeChannelLines(*timeline, id);
    assert(lines.size() == 3 && lines[0] == "Vertical push: does nothing");
    assert(lines[1].rfind("Horizontal push: taps ", 0) == 0 && lines[2].rfind("Twist: taps ", 0) == 0);
    const ModeDef *normal = builtins.find("normal");
    assert(joystickModeChannelLines(*normal, id)[0].find("works as set in the profile") != std::string::npos);
    const ModeDef *off = builtins.find("off");
    assert(joystickModeChannelLines(*off, id)[1] == "Horizontal push: does nothing");
    ChannelAction a;
    a.type = ChannelActionType::WheelV;
    assert(channelActionWords(a, id) == "scrolls vertically");
    a.type = ChannelActionType::WheelH;
    assert(channelActionWords(a, id) == "scrolls horizontally");
    a.type = ChannelActionType::Zoom;
    assert(channelActionWords(a, id) == "zooms (Ctrl + wheel)");
    a.type = ChannelActionType::Step;
    a.neg = Key::PageUp;
    a.pos = Key::PageDown;
    assert(channelActionWords(a, id).rfind("taps ", 0) == 0 && channelActionWords(a, id).find("once per push") != std::string::npos);

    // the info text: name, origin, description, channels, bound buttons
    const std::string info = joystickModeInfo(*timeline, "Built in", id);
    assert(info.find("Timeline (timeline)") == 0 && info.find("\nBuilt in\n") != std::string::npos);
    assert(info.find(timeline->description) != std::string::npos && info.find("Vertical push:") != std::string::npos);
    assert(info.find("Twist:") != std::string::npos);
    ModeDef two = plain;
    two.buttons.push_back({1, ButtonActionType::Key, Key::Space, ""});
    two.buttons.push_back({2, ButtonActionType::Nav, Key::Space, ""});
    const std::string info2 = joystickModeInfo(two, "", id);
    assert(info2.find("Mine (mine)") == 0 && info2.find("No description") != std::string::npos && info2.find("Binds 2 buttons") != std::string::npos);
    assert(info2.find("Built in") == std::string::npos);
    assert(buttonModeInfo(bm, "Your file", id).find("Action: toggle:nav") != std::string::npos);

    // the drop-down of a profile carries the description of every joystick mode
    SettingsModel m;
    ProfileSet ps;
    const size_t app = ps.findOrAdd(Section::APP, "viewer");
    m.setProfiles(ps, "");
    m.setModes(builtins, "");
    for (const SettingsModel::ModeOption &o : m.modeOptions(app))
        if (!o.id.empty()) assert(!o.description.empty() && o.description == builtins.find(o.id)->description);
    for (const SettingsModel::ModeOption &o : m.modeOptions(0)) assert(!o.description.empty());
    assert(m.modeDef("timeline") == builtins.find("timeline") || m.modeDef("timeline")->id == "timeline");
    assert(m.modeDef("nope") == nullptr);

    // the button drop-down of a profile: a button mode's description (or "No description") is its tooltip
    ButtonModeSet bs;
    ButtonMode withDesc;
    withDesc.id = "a";
    withDesc.label = "A";
    withDesc.description = "Does a";
    ButtonMode without;
    without.id = "b";
    without.label = "B";
    bs.modes = {withDesc, without};
    const std::vector<ButtonChoice> choices = buttonChoices(BindingContext::Specific, true, bs, "", id);
    int found = 0;
    for (const ButtonChoice &c : choices) {
        if (c.type == ButtonChoice::Type::ButtonMode && c.id == "a") {
            assert(c.tooltip == "Does a");
            ++found;
        }
        if (c.type == ButtonChoice::Type::ButtonMode && c.id == "b") {
            assert(c.tooltip == "No description");
            ++found;
        }
    }
    assert(found == 2);

    // the tray menu: every joystick mode entry carries its description
    TrayMenuInput in;
    in.modes = &builtins;
    in.globalMode = "normal";
    const TrayMenu menu = buildTrayMenu(in, id);
    bool sawSubmenu = false;
    for (const TrayEntry &e : menu.entries)
        if (e.kind == TrayEntry::Kind::Submenu) {
            sawSubmenu = true;
            assert(e.text == "Joystick mode" && e.children.size() == builtins.modes.size());
            for (const TrayEntry &c : e.children) assert(!c.tooltip.empty() && c.tooltip == builtins.find(c.arg)->description);
        }
    assert(sawSubmenu);

    std::puts("test_ui_mode_info: ok");
    return 0;
}
