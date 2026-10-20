// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Inheritance made explicit: where the value of a setting or a button comes from in a profile, the texts the
// profile editor shows for it, "N profiles override this", and the numbers of overrides.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/device_info_model.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/profile_export.hpp"

using namespace spn;
using namespace spn::ui;

static ModeSet modes() {
    ModeSet ms;
    ModeDef n;
    n.id = "normal";
    n.label = "normal";
    n.builtin = true;
    ms.modes.push_back(n);
    ModeDef t;
    t.id = "timeline";
    t.label = "Timeline";
    t.builtin = true;
    t.h = {ChannelActionType::Keys, Key::Left, Key::Right};
    t.overrides = {{"SCROLL_H_SENS", "21"}};
    ms.modes.push_back(t);
    return ms;
}

int main() {
    IdentityStrings id;
    SettingsModel m;
    ProfileSet ps;
    const size_t dev = ps.findOrAdd(Section::DEVICE, "Pro");
    const size_t ff = ps.findOrAdd(Section::APP, "firefox");
    const size_t gimp = ps.findOrAdd(Section::APP, "gimp");
    m.setProfiles(ps, "");
    m.setModes(modes(), "");
    std::string err;
    assert(m.setValue(0, "SCROLL_V_SENS", "7", &err));
    assert(m.setValue(dev, "SCROLL_V_SENS", "9", &err) && m.setValue(dev, "SCROLL_H_SENS", "3", &err));
    assert(m.setValue(ff, "SCROLL_V_SENS", "11", &err));

    // [global]: the plain values
    KeyOrigin o = keyOrigin(m, 0, "SCROLL_V_SENS");
    assert(o.kind == KeyOrigin::Kind::Plain && o.value == "7" && originBadgeText(o, id).empty() && o.canReset);
    o = keyOrigin(m, 0, "SCROLL_H_SENS");
    assert(o.kind == KeyOrigin::Kind::Plain && !o.canReset);  // the built-in default: nothing to reset

    // a profile that sets the key itself says so and shows what it would inherit
    o = keyOrigin(m, ff, "SCROLL_V_SENS");
    assert(o.kind == KeyOrigin::Kind::SetHere && o.value == "11" && o.inherited == "7" && o.canReset);
    assert(originBadgeText(o, id) == "Set in this profile (inherited: 7)");
    // a profile that does not: inherited from Global (set there) or from the built-in default
    o = keyOrigin(m, gimp, "SCROLL_V_SENS");
    assert(o.kind == KeyOrigin::Kind::FromGlobal && o.value == "7" && !o.canReset);
    assert(originBadgeText(o, id) == "Inherited from Global: 7; device Pro sets 9");  // the device profile sets it too
    o = keyOrigin(m, gimp, "SCROLL_H_DEADZONE");
    assert(o.kind == KeyOrigin::Kind::FromGlobalDefault);
    assert(originBadgeText(o, id) == "Inherited from Global (built-in default): " + o.value);
    // a program profile also names the device profiles that set the key (the device is not known here)
    o = keyOrigin(m, gimp, "SCROLL_H_SENS");
    assert(o.kind == KeyOrigin::Kind::FromGlobalDefault && o.deviceNames.size() == 1 && o.deviceNames[0] == "Pro" && o.deviceValues[0] == "3");
    assert(originBadgeText(o, id).find("; device Pro sets 3") != std::string::npos);
    // a device profile does not name devices (it is one)
    assert(keyOrigin(m, dev, "SCROLL_V_SENS").deviceNames.empty());
    assert(originBadgeText(keyOrigin(m, dev, "SCROLL_V_SENS"), id) == "Set in this profile (inherited: 7)");

    // the joystick mode overrides a value: the origin says so, in Global too
    assert(m.setValue(0, "MODE", "timeline", &err));
    o = keyOrigin(m, gimp, "SCROLL_H_SENS");
    assert(o.kind == KeyOrigin::Kind::FromMode && o.modeId == "timeline" && o.value == "21");
    assert(originBadgeText(o, id) == "From joystick mode \"timeline\": 21; device Pro sets 3");
    o = keyOrigin(m, 0, "SCROLL_H_SENS");
    assert(o.kind == KeyOrigin::Kind::FromMode && originBadgeText(o, id) == "From joystick mode \"timeline\": 21");
    assert(m.setValue(0, "MODE", "normal", &err));

    // buttons
    assert(m.setButton(0, 2, "nav", &err) && m.setButton(ff, 2, "key:space", &err) && m.setButton(dev, 3, "click", &err));
    o = buttonOrigin(m, 0, 2);
    assert(o.kind == KeyOrigin::Kind::Plain && o.value == "nav");
    o = buttonOrigin(m, ff, 2);
    assert(o.kind == KeyOrigin::Kind::SetHere && o.value == "key:space" && o.inherited == "nav" && o.canReset);
    assert(originBadgeText(o, id) == "Set in this profile (inherited: nav)");
    o = buttonOrigin(m, gimp, 2);
    assert(o.kind == KeyOrigin::Kind::FromGlobal && o.value == "nav" && originBadgeText(o, id) == "Inherited from Global: nav");
    o = buttonOrigin(m, gimp, 3);
    assert(o.kind == KeyOrigin::Kind::FromGlobalDefault && o.value == "none" && o.deviceNames.size() == 1 && o.deviceValues[0] == "click");
    assert(originBadgeText(o, id).find("device Pro sets click") != std::string::npos);

    // Global: "N profiles override this" and the list for the tooltip
    std::vector<std::string> names;
    assert(overrideHint(m, "SCROLL_V_SENS", id, &names) == "2 profiles override this");
    assert(names.size() == 2 && names[0] == "device Pro" && names[1] == "program firefox");
    assert(overrideHint(m, "SCROLL_H_SENS", id, &names) == "1 profile overrides this" && names.size() == 1);
    assert(overrideHint(m, "SCROLL_H_DEADZONE", id).empty());
    assert(overrideHint(m, "BUTTON_2", id) == "1 profile overrides this");
    assert(overrideHintTooltip(names, id).find("device Pro") != std::string::npos);
    assert(m.profilesOverriding("BUTTON_3").size() == 1 && m.profilesOverriding("MODE").empty());

    // reset tooltips
    assert(resetTooltip(0, true, id) == "Back to the built-in default" && resetTooltip(0, false, id) == "Already the built-in default");
    assert(resetTooltip(ff, true, id).find("Reset to inherited") == 0 && resetTooltip(ff, false, id).find("Nothing to reset") == 0);

    // header sentence, list labels, counts
    assert(headerExplanation(0, Section::GLOBAL, id) == "Applies to every device and program unless a device or program profile overrides it.");
    assert(headerExplanation(ff, Section::APP, id) == "Everything you do not change here is inherited from Global (and the device).");
    assert(m.overrideCount(ff) == 2 && m.overrideCount(gimp) == 0 && m.overrideCount(99) == 0);
    assert(profileListLabel("firefox", 2) == "firefox (2)" && profileListLabel("gimp", 0) == "gimp");
    assert(profileOverrideCountText(0, id) == "Overrides nothing: everything is inherited");
    assert(profileOverrideCountText(1, id) == "Overrides 1 setting" && profileOverrideCountText(5, id) == "Overrides 5 settings");

    // export of a profile
    assert(profileExportFileName(m, ff) == "firefox.conf" && profileExportFileName(m, 0) == "global.conf");
    ps = ProfileSet();
    ps.findOrAdd(Section::APP, "a/b:c");
    SettingsModel x;
    x.setProfiles(ps, "");
    assert(profileExportFileName(x, 1) == "a_b_c.conf");
    const std::string text = profileExportText(m, ff);
    assert(text.find("[app:firefox]\n") != std::string::npos && text.find("SCROLL_V_SENS = 11") != std::string::npos &&
           text.find("BUTTON_2 = key:space") != std::string::npos && text.find("[global]") == std::string::npos);
    assert(profileExportText(m, 99).empty());
    assert(profileExportText(m, 0).find("[global]\n") != std::string::npos);

    // device info
    DeviceInfo a;
    a.name = "3Dconnexion SpaceMouse Pro";
    a.reportedButtons = 0;
    DeviceInfo b;
    b.name = "Some Unknown Mouse";
    b.reportedButtons = 12;
    DeviceInfo c;
    c.name = "Unknown Without Count";
    const std::vector<DeviceInfoLine> lines = buildDeviceInfo({a, b, c});
    assert(lines.size() == 3 && lines[0].buttons > 0 && lines[0].fromTable);
    assert(lines[1].buttons == 12 && !lines[1].fromTable && lines[2].buttons == 0);
    assert(deviceInfoText(lines[1], id) == "Some Unknown Mouse: 12 buttons, 6 axes");
    assert(deviceInfoText(lines[2], id) == "Unknown Without Count: number of buttons unknown, 6 axes");
    assert(deviceButtonSource(lines[0], id).find("table of known models") != std::string::npos);
    assert(deviceButtonSource(lines[1], id).find("reported by the driver") != std::string::npos);
    assert(deviceButtonSource(lines[2], id).find("Up to 32") != std::string::npos);

    std::puts("test_ui_inheritance: ok");
    return 0;
}
