// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Per-profile editing of motions and buttons (motion_edit_model.hpp): every motion on its own is inherited, set here
// or cleared here (V_PUSH_ACTION ... TWIST_ACTION in the section of the profile, no other key), another action,
// clear, switch on, go back to inheriting, the words of each state, the summary texts that must say "off" and never
// show a cleared action, buttons of a program over the mode's, and the resolution in the config layer.
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

static std::string sectionText(const SettingsModel &m, size_t i) {
    std::string t;
    for (const KV &kv : m.profiles().sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}

static ChannelForm form(ChannelActionType t, const char *neg = "up", const char *pos = "down") {
    ChannelForm f;
    f.type = t;
    f.neg = neg;
    f.pos = pos;
    return f;
}

static const MotionRow *row(const ProfileAssignment &a, const char *id) {
    for (const MotionRow &r : a.motions)
        if (r.id == id) return &r;
    return nullptr;
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
    assert(e.editable && e.key == "TWIST_ACTION" && e.state == MotionState::Inherited && e.on && e.canClear && !e.canInherit && !e.canSwitchOn);
    assert(has(e.words, "On.") && has(e.words, "Global"));
    assert(clearMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m, code) == "TWIST_ACTION=none;");  // the one key, nothing else
    assert(m.effective(0).actions[0].any() == false);      // Global untouched
    e = motionEdit(m, code, "twist", id);
    assert(e.state == MotionState::Cleared && !e.on && !e.canClear && e.canInherit && e.canSwitchOn);
    assert(has(e.words, "Cleared in this profile") && has(e.words, "inherited: on"));
    assert(motionEdit(m, code, "v_push", id).state == MotionState::Inherited);  // the others inherit

    ProfileAssignment a = profileAssignment(m, code, id);
    const MotionRow *tw = row(a, "twist");
    assert(tw && tw->kind == MotionKind::Off && tw->chip == "Off" && tw->source == "cleared in this profile");
    assert(!has(motionChipText(*tw), "Wheel") && !has(tw->action, "Ctrl"));
    a = profileAssignment(m, dev, id);  // another profile still zooms
    assert(row(a, "twist")->kind == MotionKind::Active && has(row(a, "twist")->action, "Zoom"));
    assert(originBadgeText(keyOrigin(m, code, "ZOOM_SENS"), id).find("Inherited") == 0);  // the tuning is untouched
    assert(displayValue("ZOOM_SENS", "0", id) == "0 (off)");

    // ---- back: inherit removes the key, switch on gives the old action back ----
    assert(inheritMotion(m, code, "twist", &mem));
    assert(sectionText(m, code).empty() && motionEdit(m, code, "twist", id).state == MotionState::Inherited);
    {  // the pointer is a choice of every motion (and of the lift), the write is the plain action text
        int n = 0, nl = 0;
        for (const MotionActionChoice &c : motionActionChoices()) n += c.type == ChannelActionType::PointerX || c.type == ChannelActionType::PointerY;
        for (const MotionActionChoice &c : liftActionChoices()) nl += c.type == ChannelActionType::PointerX || c.type == ChannelActionType::PointerY;
        assert(n == 2 && nl == 2);
        assert(setMotionAction(m, code, "h_push", form(ChannelActionType::PointerX, "", ""), &err));
        assert(sectionText(m, code) == "H_PUSH_ACTION=pointer_x;");
        assert(inheritMotion(m, code, "h_push", &mem) && sectionText(m, code).empty());
    }
    assert(setMotionAction(m, code, "twist", form(ChannelActionType::Keys, "ctrl+minus", "ctrl+plus"), &err));  // another action
    assert(sectionText(m, code) == "TWIST_ACTION=keys:ctrl+minus,ctrl+plus;");
    e = motionEdit(m, code, "twist", id);
    assert(e.state == MotionState::SetHere && e.on && e.action.type == ChannelActionType::Keys && e.canInherit);
    assert(has(e.words, "Set in this profile"));
    tw = nullptr;
    a = profileAssignment(m, code, id);
    assert(row(a, "twist")->kind == MotionKind::Active && has(row(a, "twist")->action, "Keys") && row(a, "twist")->source == "set in this profile");
    assert(clearMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m, code) == "TWIST_ACTION=none;");
    assert(switchOnMotion(m, code, "twist", &mem, &err));  // the user's own action is back
    assert(sectionText(m, code) == "TWIST_ACTION=keys:ctrl+minus,ctrl+plus;");
    assert(inheritMotion(m, code, "twist", &mem) && sectionText(m, code).empty());
    assert(clearMotion(m, code, "twist", &mem, &err) && switchOnMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m, code).empty());  // nothing remembered: the override simply goes

    // ---- an invalid action is never written ----
    assert(!m.setValue(code, "TWIST_ACTION", "keys:left", &err) && !err.empty() && sectionText(m, code).empty());
    assert(!setMotionAction(m, code, "twist", form(ChannelActionType::Keys, "left", "nosuchkey"), &err) && sectionText(m, code).empty());

    // ---- each of the six motions on its own: push and tilt of a direction are separate ----
    assert(clearMotion(m, code, "v_tilt", &mem, &err));
    assert(sectionText(m, code) == "V_TILT_ACTION=none;");
    assert(!motionEdit(m, code, "v_tilt", id).on && motionEdit(m, code, "v_push", id).on);
    assert(motionEdit(m, code, "v_push", id).state == MotionState::Inherited && motionEdit(m, code, "v_tilt", id).state == MotionState::Cleared);
    a = profileAssignment(m, code, id);
    assert(row(a, "v_tilt")->kind == MotionKind::Off && row(a, "v_push")->kind == MotionKind::Active);
    assert(clearMotion(m, code, "v_push", &mem, &err) && !motionEdit(m, code, "v_push", id).on);
    assert(inheritMotion(m, code, "v_tilt", &mem) && sectionText(m, code) == "V_PUSH_ACTION=none;");
    assert(motionEdit(m, code, "v_tilt", id).on && !motionEdit(m, code, "v_push", id).on);
    assert(setMotionAction(m, code, "h_tilt", form(ChannelActionType::Step, "pageup", "pagedown"), &err));
    assert(motionEdit(m, code, "h_push", id).state == MotionState::Inherited && motionEdit(m, code, "h_tilt", id).action.type == ChannelActionType::Step);
    for (const char *id3 : {"v_push", "h_tilt"}) assert(inheritMotion(m, code, id3, &mem));
    assert(sectionText(m, code).empty());

    // ---- lift / press: its own key LIFT_ACTION, off by default, no analog keys ----
    {
        MotionEdit l = motionEdit(m, code, "lift", id);
        assert(l.editable && l.key == "LIFT_ACTION" && l.prefix == "LIFT_" && !l.on && !l.canSwitchOn && l.state == MotionState::Inherited);
        assert(row(profileAssignment(m, code, id), "lift")->kind == MotionKind::NotUsed);
        assert(!setMotionAction(m, code, "lift", form(ChannelActionType::Keys, "left", "right"), &err) && has(err, "step:"));
        assert(sectionText(m, code).empty());
        assert(setMotionAction(m, code, "lift", form(ChannelActionType::WheelV), &err));
        assert(sectionText(m, code) == "LIFT_ACTION=wheel_v;");
        l = motionEdit(m, code, "lift", id);
        assert(l.on && l.state == MotionState::SetHere && l.canInherit && l.action.type == ChannelActionType::WheelV);
        const ProfileAssignment pa = profileAssignment(m, code, id);
        const MotionRow *lr = row(pa, "lift");
        assert(lr->kind == MotionKind::Active && lr->action == "Scroll" && lr->source == "set in this profile");
        assert(setMotionAction(m, code, "lift", form(ChannelActionType::Step, "pageup", "pagedown"), &err));
        assert(sectionText(m, code) == "LIFT_ACTION=step:pageup,pagedown;");
        assert(m.setValue(code, "LIFT_DEADZONE", "120", &err) && m.setValue(code, "LIFT_SENS", "7", &err));
        // clear pins none, "use the inherited setting" removes the key again (and the tuning of the lift)
        assert(clearMotion(m, code, "lift", &mem, &err) && sectionText(m, code).find("LIFT_ACTION=none;") != std::string::npos);
        assert(motionEdit(m, code, "lift", id).state == MotionState::Cleared && row(profileAssignment(m, code, id), "lift")->kind == MotionKind::Off);
        assert(inheritMotion(m, code, "lift", &mem));
        assert(m.isOverridden(code, "LIFT_ACTION") == false);
        for (const char *k : {"LIFT_MIX", "LIFT_SENS", "LIFT_DEADZONE", "LIFT_SIGN", "LIFT_CURVE", "LIFT_SMOOTH_MS"}) m.clearOverride(code, k);
        assert(sectionText(m, code).empty());
        // Global sets it, a profile inherits it; the profile's own none beats it (precedence global < device < program)
        assert(setMotionAction(m, 0, "lift", form(ChannelActionType::Zoom), &err));
        assert(motionEdit(m, code, "lift", id).on && motionEdit(m, code, "lift", id).action.type == ChannelActionType::Zoom);
        assert(clearMotion(m, code, "lift", &mem, &err) && !motionEdit(m, code, "lift", id).on);
        assert(inheritMotion(m, code, "lift", &mem) && motionEdit(m, code, "lift", id).on);
        assert(inheritMotion(m, 0, "lift", &mem) && !motionEdit(m, 0, "lift", id).on);
        assert(sectionText(m, code).empty() && sectionText(m, 0).find("LIFT_ACTION") == std::string::npos);
    }

    // ---- a legacy <CH>_SENS = 0 is read as cleared and goes with "Use the inherited setting" ----
    assert(m.setValue(code, "ZOOM_SENS", "0", &err));
    assert(motionEdit(m, code, "twist", id).state == MotionState::Cleared && row(profileAssignment(m, code, id), "twist")->kind == MotionKind::Off);
    assert(switchOnMotion(m, code, "twist", &mem, &err) && sectionText(m, code).empty());
    assert(m.setValue(code, "ZOOM_SENS", "0", &err) && inheritMotion(m, code, "twist", &mem) && sectionText(m, code).empty());

    // ---- Global is off: "switch on" never leaves nothing, it writes the normal action ----
    assert(clearMotion(m, 0, "twist", &mem, &err));
    assert(sectionText(m, 0) == "TWIST_ACTION=none;" && has(motionEdit(m, 0, "twist", id).words, "Cleared in Global"));
    e = motionEdit(m, code, "twist", id);
    assert(!e.on && e.state == MotionState::Inherited && e.canSwitchOn && has(e.words, "Off."));
    assert(switchOnMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m, code) == "TWIST_ACTION=default;" && motionEdit(m, code, "twist", id).on);
    assert(inheritMotion(m, code, "twist", &mem) && !motionEdit(m, code, "twist", id).on);  // inherits "off" again
    assert(switchOnMotion(m, 0, "twist", &mem, &err) && sectionText(m, 0).empty());

    // a sensitivity of 0 in Global: the factory sensitivity goes into the profile
    assert(m.setValue(0, "ZOOM_SENS", "0", &err));
    assert(switchOnMotion(m, code, "twist", &mem, &err));
    assert(sectionText(m, code) == "ZOOM_SENS=" + std::to_string(Settings().zoom.sensitivity) + ";");
    assert(inheritMotion(m, code, "twist", &mem) || true);
    assert(m.clearOverride(0, "ZOOM_SENS"));
    for (const char *k : {"ZOOM_SENS"}) m.clearOverride(code, k);

    // ---- a joystick mode below the program: a device profile cannot beat it, the program can ----
    {
        SettingsModel mm;
        ProfileSet p2;
        const size_t d2 = p2.findOrAdd(Section::DEVICE, "Pro");
        const size_t a2 = p2.findOrAdd(Section::APP, "code");
        p2.sections[d2].kv = {{"MODE", "timeline"}};
        p2.sections[a2].kv = {{"MODE", "timeline"}};
        mm.setProfiles(p2, "");
        mm.setModes(modes, "");
        // timeline: the twist keys left,right; the vertical channel off
        assert(!setMotionAction(mm, d2, "twist", form(ChannelActionType::Zoom), &err) && has(err, "joystick mode"));
        assert(sectionText(mm, d2) == "MODE=timeline;");  // nothing written
        assert(setMotionAction(mm, a2, "twist", form(ChannelActionType::Zoom), &err));  // the program wins
        assert(motionEdit(mm, a2, "twist", id).action.type == ChannelActionType::Zoom);
        assert(setMotionAction(mm, a2, "v_push", form(ChannelActionType::WheelV), &err));  // the mode switched the channel off
        assert(motionEdit(mm, a2, "v_push", id).on);
    }

    // ---- Global itself ----
    assert(clearMotion(m, 0, "h_push", &mem, &err) && motionEdit(m, 0, "h_tilt", id).on && !motionEdit(m, 0, "h_push", id).on);
    assert(inheritMotion(m, 0, "h_push", &mem) && sectionText(m, 0).empty());

    // ---- buttons: set here, cleared here (explicit none over the device's binding), inherited ----
    assert(m.setButton(dev, 2, "key:space", &err));
    a = profileAssignment(m, code, id);
    assert(a.buttons.empty());  // a program profile sees Global only; the device's binding is named in the origin text
    assert(m.setButton(code, 2, "none", &err));
    assert(m.isOverridden(code, "BUTTON_2") && sectionText(m, code) == "BUTTON_2=none;");
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

    // a button the joystick mode binds: a program profile's own binding wins over it, a device profile's does not
    {
        ModeSet ms = modes;
        ModeDef bm;
        bm.id = "bound";
        bm.label = "bound";
        bm.buttons.push_back({1, ButtonActionType::Key, Key::Space, "", {}});
        ms.modes.push_back(bm);
        SettingsModel mm;
        ProfileSet p2;
        const size_t d2 = p2.findOrAdd(Section::DEVICE, "Pro");
        const size_t a2 = p2.findOrAdd(Section::APP, "code");
        p2.sections[0].kv = {{"MODE", "bound"}};
        p2.sections[d2].kv = {{"BUTTON_1", "click"}};
        p2.sections[a2].kv = {{"BUTTON_1", "rightclick"}};
        mm.setProfiles(p2, "");
        mm.setModes(ms, "");
        a = profileAssignment(mm, d2, id);
        assert(a.buttons.size() >= 1 && a.buttons[0].source == "from the joystick mode");
        a = profileAssignment(mm, a2, id);
        assert(a.buttons.size() >= 1 && a.buttons[0].source == "from this profile" && has(a.buttons[0].tooltip, "rightclick"));
        assert(mm.setButton(a2, 1, "none", &err));  // the program unbinds the button although the mode binds it
        a = profileAssignment(mm, a2, id);
        assert(a.buttons[0].action == "Cleared");
    }

    // ---- the config layer: the keys are accepted in every section, written only when set, layered ----
    {
        ProfileSet f;
        f.sections[0].kv = {{"BUTTON_1", "key:space"}, {"BUTTON_3", "click"}, {"H_ACTION", "none"}};
        const size_t fd = f.findOrAdd(Section::DEVICE, "Pro");
        f.sections[fd].kv = {{"BUTTON_1", "rightclick"}, {"BUTTON_2", "middleclick"}, {"V_TILT_ACTION", "wheel_v"}};
        const size_t fa = f.findOrAdd(Section::APP, "code");
        f.sections[fa].kv = {{"BUTTON_1", "key:ctrl+s"}, {"BUTTON_2", "none"}, {"TWIST_ACTION", "none"}, {"H_ACTION", "wheel_h"}};
        const Settings plain = f.resolve("SpaceMouse Pro", "firefox");
        assert(plain.buttons.at(1) == "rightclick" && plain.buttons.at(2) == "middleclick" && plain.buttons.at(3) == "click");
        assert(plain.app_buttons.empty() && plain.actions[kLayerApp].any() == false);
        assert(plain.actions[kLayerGlobal].chanSet[ProfileActions::H] && plain.actions[kLayerDevice].partSet[PART_V_TILT]);
        const Settings vs = f.resolve("SpaceMouse Pro", "Code");
        assert(vs.buttons.at(1) == "key:ctrl+s" && vs.buttons.at(2) == "none" && vs.buttons.at(3) == "click");
        assert(vs.app_buttons.count(1) && vs.app_buttons.count(2) && !vs.app_buttons.count(3));
        assert(vs.actions[kLayerApp].chanSet[ProfileActions::TWIST] && vs.actions[kLayerApp].chanSet[ProfileActions::H]);
        const ChannelActions ca = effectiveChannelActions(ModeDef(), vs);
        assert(ca.twist.type == ChannelActionType::Off && ca.h.type == ChannelActionType::WheelH);
        assert(ca.vSplit.tiltSet && ca.vSplit.tilt.type == ChannelActionType::WheelV && !ca.vSplit.pushSet);
        // the keys round trip through allKeys (canonical text) and the file has them only where set
        const std::vector<KV> keys = allKeys(vs);
        bool seen = false;
        for (const KV &kv : keys) seen = seen || (kv.first == "TWIST_ACTION" && kv.second == "none");
        assert(seen);
        bool anyAction = false;
        for (const KV &kv : allKeys(Settings())) anyAction = anyAction || kv.first.find("_ACTION") != std::string::npos;
        assert(!anyAction);  // a config without them is written exactly as before
    }

    std::puts("test_ui_motion_edit: OK");
    return 0;
}
