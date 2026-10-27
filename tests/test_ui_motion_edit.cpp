// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Per-profile editing of motions and buttons (motion_edit_model.hpp): clear a motion ("zoom off for one program"),
// switch it on again, go back to inheriting, the words of each state, the summary texts that must say "off" and
// never show a cleared action, and the daemon-side resolution of BUTTON_<n> (program over device over Global, an
// explicit "none" unbinds).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "testutil.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

static bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

static std::string sectionText(const ProfileSet &ps, size_t i) {
    std::string t;
    for (const KV &kv : ps.sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}

int main() {
    useFreshDefaultOf("linux");  // not the host system
    IdentityStrings id;
    ModeSet modes = loadModes("/nonexistent-dir-for-test", nullptr);
    ProfileSet ps;
    const size_t dev = ps.findOrAdd(Section::DEVICE, "Pro");
    const size_t code = ps.findOrAdd(Section::APP, "code");
    SettingsModel m;
    m.setProfiles(ps, "");
    m.setModes(modes, "");
    MotionMemory mem;
    std::string err;

    // ---- zoom off for one program, everything else inherited ----
    MotionEdit e = motionEdit(m, code, "twist", id);
    assert(e.editable && e.state == MotionState::Inherited && e.on && e.canClear && !e.canInherit && !e.canSwitchOn);
    assert(has(e.words, "On.") && has(e.words, "Global"));
    assert(clearMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m.profiles(), code) == "ZOOM_SENS=0;");  // the one key, nothing else
    assert(m.effective(0).zoom.sensitivity == 5);                // Global untouched
    e = motionEdit(m, code, "twist", id);
    assert(e.state == MotionState::Cleared && !e.on && !e.canClear && e.canInherit && e.canSwitchOn);
    assert(has(e.words, "Cleared in this profile") && has(e.words, "Global: on"));
    // the other channels still inherit
    assert(motionEdit(m, code, "v_push", id).state == MotionState::Inherited);

    // the summary says off in words, the chip never shows the zoom keys
    ProfileAssignment a = profileAssignment(m, code, id);
    const MotionRow *tw = nullptr;
    for (const MotionRow &r : a.motions)
        if (r.id == "twist") tw = &r;
    assert(tw && tw->kind == MotionKind::Off && tw->chip == "Off" && tw->source == "cleared in this profile");
    assert(!has(motionChipText(*tw), "Wheel") && !has(tw->action, "Ctrl"));
    // another profile still zooms
    a = profileAssignment(m, dev, id);
    for (const MotionRow &r : a.motions)
        if (r.id == "twist") assert(r.kind == MotionKind::Active && has(r.action, "Zoom"));
    // the origin text of the key names the value in words
    assert(originBadgeText(keyOrigin(m, code, "ZOOM_SENS"), id) == "Set in this profile (inherited: 5)");
    assert(displayValue("ZOOM_SENS", "0", id) == "0 (off)");

    // ---- back: switch on restores the number the profile had; inheriting removes the override ----
    assert(switchOnMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m.profiles(), code).empty());  // inherited again: Global's 5 applies, no override left
    assert(motionEdit(m, code, "twist", id).state == MotionState::Inherited);
    assert(m.setValue(code, "ZOOM_SENS", "9", &err));
    assert(motionEdit(m, code, "twist", id).state == MotionState::SetHere);
    assert(clearMotion(m, code, "twist", &mem, &err));
    assert(switchOnMotion(m, code, "twist", &mem, &err));  // the user's own 9 is back
    assert(sectionText(m.profiles(), code) == "ZOOM_SENS=9;");
    assert(clearMotion(m, code, "twist", &mem, &err));
    assert(inheritMotion(m, code, "twist", &mem));  // "Use the inherited setting" on a cleared motion: only the 0 goes
    assert(sectionText(m.profiles(), code).empty());

    // ---- all three channels; the push and tilt of a channel are one channel ----
    for (const char *id3 : {"v_push", "v_tilt", "h_push", "h_tilt", "twist"}) {
        assert(clearMotion(m, code, id3, &mem, &err));
        assert(!motionEdit(m, code, id3, id).on);
    }
    assert(!motionEdit(m, code, "v_tilt", id).on && sectionText(m.profiles(), code).find("SCROLL_V_SENS=0;") == 0);
    assert(motionEdit(m, code, "v_push", id).shared == "Vertical tilt");
    for (const char *id3 : {"v_push", "h_push", "twist"}) assert(inheritMotion(m, code, id3, &mem));
    assert(sectionText(m.profiles(), code).empty());
    assert(!motionEdit(m, code, "lift", id).editable && !clearMotion(m, code, "lift", &mem, &err));

    // ---- Global is off: "switch on" never leaves 0, it writes the factory sensitivity ----
    assert(m.setValue(0, "ZOOM_SENS", "0", &err));
    e = motionEdit(m, code, "twist", id);
    assert(!e.on && e.state == MotionState::Inherited && e.canSwitchOn && has(e.words, "Off."));
    assert(switchOnMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m.profiles(), code) == "ZOOM_SENS=" + std::to_string(Settings().zoom.sensitivity) + ";");
    assert(motionEdit(m, code, "twist", id).on);
    assert(inheritMotion(m, code, "twist", &mem));
    assert(!motionEdit(m, code, "twist", id).on);  // inherits "off" again
    assert(m.clearOverride(0, "ZOOM_SENS"));

    // ---- Global itself ----
    assert(clearMotion(m, 0, "h_push", &mem, &err) && !motionEdit(m, 0, "h_tilt", id).on);
    assert(has(motionEdit(m, 0, "h_push", id).words, "Cleared in Global"));
    assert(switchOnMotion(m, 0, "h_push", &mem, &err) && m.effective(0).scroll_h.sensitivity == 3);

    // ---- buttons: set here, cleared here (explicit none over the device's binding), inherited ----
    assert(m.setButton(dev, 2, "key:space", &err));
    a = profileAssignment(m, code, id);
    assert(a.buttons.empty());  // a program profile sees Global only; the device's binding is named in the origin text
    assert(m.setButton(code, 2, "none", &err));
    assert(m.isOverridden(code, "BUTTON_2") && sectionText(m.profiles(), code) == "BUTTON_2=none;");
    a = profileAssignment(m, code, id);
    assert(a.buttons.size() == 1 && a.buttons[0].number == 2 && a.buttons[0].muted && a.buttons[0].action == "Cleared");
    assert(a.buttons[0].source == "cleared in this profile" && !has(a.buttons[0].tooltip, "space"));
    const KeyOrigin bo = buttonOrigin(m, code, 2);
    assert(bo.kind == KeyOrigin::Kind::SetHere && bo.canReset && originBadgeText(bo, id).find("Set in this profile") == 0);
    assert(m.setButton(code, 2, "key:ctrl+s", &err));  // another binding here
    a = profileAssignment(m, code, id);
    assert(a.buttons.size() == 1 && has(a.buttons[0].tooltip, "key:ctrl+s") && a.buttons[0].source == "from this profile");
    assert(m.clearButton(code, 2));  // back to inheriting
    assert(!m.isOverridden(code, "BUTTON_2") && profileAssignment(m, code, id).buttons.empty());

    // ---- the daemon's resolution (config.cpp): program over device over Global, "none" unbinds ----
    ProfileSet f;
    f.sections[0].kv = {{"BUTTON_1", "key:space"}, {"BUTTON_3", "click"}};
    const size_t fd = f.findOrAdd(Section::DEVICE, "Pro");
    f.sections[fd].kv = {{"BUTTON_1", "rightclick"}, {"BUTTON_2", "middleclick"}, {"ZOOM_SENS", "7"}};
    const size_t fa = f.findOrAdd(Section::APP, "code");
    f.sections[fa].kv = {{"BUTTON_1", "key:ctrl+s"}, {"BUTTON_2", "none"}, {"ZOOM_SENS", "0"}};
    const Settings plain = f.resolve("SpaceMouse Pro", "firefox");
    assert(plain.buttons.at(1) == "rightclick" && plain.buttons.at(2) == "middleclick" && plain.buttons.at(3) == "click");
    assert(plain.zoom.sensitivity == 7);
    const Settings vs = f.resolve("SpaceMouse Pro", "Code");
    assert(vs.buttons.at(1) == "key:ctrl+s");           // the program's binding over the device's
    assert(vs.buttons.at(2) == "none");                  // explicit unbind over the device's binding
    assert(vs.buttons.at(3) == "click");                 // everything else inherited
    assert(vs.zoom.sensitivity == 0 && vs.scroll_v.sensitivity == 5);  // zoom off, the other channels as before

    std::puts("test_ui_motion_edit: OK");
    return 0;
}
