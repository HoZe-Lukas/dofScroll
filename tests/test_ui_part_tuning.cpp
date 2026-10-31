// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The tuning of ONE motion (part_tuning_model.hpp, part_tuning.hpp): a profile edit of the curve or the direction of the vertical
// tilt writes only SCROLL_V_TILT_CURVE / SCROLL_V_TILT_SIGN (and splits the channel without changing what any motion does), the
// push keeps the values it inherits, the settings the runner gets give push and tilt different curves and directions, and the
// runner does the same as before for each motion on its own. Also the joystick mode form (splitForEdit).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "mode_runner.hpp"
#include "testutil.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/part_tuning_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

struct PtRec : Output {
    int wv = 0, wh = 0, taps = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { wv += n; }
    void wheelH(int n) override { wh += n; }
    void setKey(Key, bool) override {}
    void tapKey(Key) override { ++taps; }
    void mouseButton(int, bool) override {}
    void releaseAll() override {}
    void flush() override {}
};

bool ptHas(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

std::string ptSection(const SettingsModel &m, size_t i) {
    std::string t;
    for (const KV &kv : m.profiles().sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}

// What one motion does alone: the wheel steps of 1 s with the cap deflected on one axis, under the settings of the profile.
int ptRun(const SettingsModel &m, size_t section, Axis axis, int value, bool horizontal = false, int *taps = nullptr) {
    PtRec o;
    ModeRunner r(&o);
    const ModeDef *def = m.modeDef(m.effectiveMode(section));
    if (def) r.setMode(*def);
    const Settings s = m.effectiveWithMode(section);
    RawAxes raw{};
    raw[size_t(axis)] = value;
    for (int i = 0; i < 250; ++i) r.tick(s, raw, 0, true, 1.0 / 250.0);
    if (taps) *taps = o.taps;
    return horizontal ? o.wh : o.wv;
}

const PartTuningField *ptField(const PartTuningView &v, const char *suffix) {
    for (const PartTuningField &f : v.fields)
        if (f.suffix == suffix) return &f;
    return nullptr;
}

struct PtRig {
    SettingsModel m;
    size_t dev = 0, code = 0;
    PtRig() {
        useFreshDefaultOf("linux");
        ProfileSet ps;
        dev = ps.findOrAdd(Section::DEVICE, "Pro");
        code = ps.findOrAdd(Section::APP, "code");
        m.setProfiles(ps, "");
        m.setModes(loadModes("/nonexistent-dir-for-test", nullptr), "");
    }
};

}  // namespace

static void testPartEditWritesOnlyThePart() {
    PtRig rig;
    SettingsModel &m = rig.m;
    IdentityStrings id;
    std::string err;
    const SettingsModel before = m;
    const int pushBefore = ptRun(before, rig.code, AX_Z, 300), tiltBefore = ptRun(before, rig.code, AX_RX, 300);
    assert(pushBefore > 0 && tiltBefore > 0);

    assert(setPartTuning(m, rig.code, "v_tilt", "CURVE", "2", &err));
    assert(setPartTuning(m, rig.code, "v_tilt", "SIGN", "-1", &err));
    const std::string text = ptSection(m, rig.code);
    // the keys of the tilt, and the two part actions that make the keys act ("default": the channel had no action of its own)
    assert(ptHas(text, "SCROLL_V_TILT_CURVE=2;") && ptHas(text, "SCROLL_V_TILT_SIGN=-1;"));
    assert(ptHas(text, "V_PUSH_ACTION=default;") && ptHas(text, "V_TILT_ACTION=default;"));
    // nothing else: no key of the push, no key of the whole channel, nothing of the horizontal direction
    assert(!ptHas(text, "SCROLL_V_PUSH_") && !ptHas(text, "SCROLL_V_CURVE") && !ptHas(text, "SCROLL_V_SIGN") && !ptHas(text, "SCROLL_H") && !ptHas(text, "H_PUSH") &&
           !ptHas(text, "H_TILT"));
    assert(ptSection(m, 0).empty() && ptSection(m, rig.dev).empty());  // Global and the device are untouched

    // the effective settings give the two motions different curves and directions
    const Settings s = m.effectiveWithMode(rig.code);
    const ChannelCfg push = partConfig(s.scroll_v, s.parts[PART_V_PUSH], Mix()), tilt = partConfig(s.scroll_v, s.parts[PART_V_TILT], Mix());
    assert(push.curve_exp == 1.0 && tilt.curve_exp == 2.0 && push.sign == 1 && tilt.sign == -1);
    assert(push.sensitivity == tilt.sensitivity && push.deadzone == tilt.deadzone);  // the rest follows the channel
    assert(effectiveChannelActions(*m.modeDef("normal"), s).vSplit.active());

    // the runner: the push does what it did, the tilt runs the other way
    assert(ptRun(m, rig.code, AX_Z, 300) == pushBefore);
    assert(ptRun(m, rig.code, AX_RX, 300) < 0);

    // the view: the push inherits from the whole channel, the tilt says it is its own
    const PartTuningView vt = partTuningView(m, rig.code, "v_tilt", id), vp = partTuningView(m, rig.code, "v_push", id);
    assert(vt.partMotion && vt.split && vp.split);
    assert(ptField(vt, "CURVE")->own && ptField(vt, "CURVE")->value == "2" && ptHas(ptField(vt, "CURVE")->words, "Set in this profile for this motion only"));
    assert(ptField(vt, "SIGN")->value == "-1" && ptHas(ptField(vt, "SIGN")->words, "inherited: normal"));
    assert(!ptField(vp, "CURVE")->own && ptField(vp, "CURVE")->value == "1" && ptHas(ptField(vp, "CURVE")->words, "Inherited from the whole channel"));
    assert(!ptField(vp, "SIGN")->own && ptField(vp, "SIGN")->value == "1");

    // the other direction and the other fields are not touched; the push of the same direction can be tuned on its own as well
    assert(setPartTuning(m, rig.code, "v_push", "SENS", "9", &err));
    const Settings s2 = m.effectiveWithMode(rig.code);
    assert(s2.parts[PART_V_PUSH].cfg.sensitivity == 9 && !(s2.parts[PART_V_TILT].set & PartTuning::F_SENS));
    assert(partConfig(s2.scroll_v, s2.parts[PART_V_TILT], Mix()).sensitivity == s2.scroll_v.sensitivity);
    assert(!(s2.parts[PART_V_PUSH].set & PartTuning::F_CURVE) && !(s2.parts[PART_V_PUSH].set & PartTuning::F_SIGN));

    // a change of the whole channel in Global is still the starting value of every motion without a value of its own
    assert(m.setValue(0, "SCROLL_V_DEADZONE", "55", &err));
    const Settings s3 = m.effectiveWithMode(rig.code);
    assert(partConfig(s3.scroll_v, s3.parts[PART_V_PUSH], Mix()).deadzone == 55 && partConfig(s3.scroll_v, s3.parts[PART_V_TILT], Mix()).deadzone == 55);
    assert(ptHas(ptField(partTuningView(m, rig.code, "v_tilt", id), "DEADZONE")->words, "Inherited from the whole channel: 55 (Inherited from Global"));

    // the way back: the motion follows the channel again for that field
    assert(clearPartTuning(m, rig.code, "v_tilt", "CURVE") && clearPartTuning(m, rig.code, "v_tilt", "SIGN"));
    assert(!ptHas(ptSection(m, rig.code), "SCROLL_V_TILT_"));
    const Settings s4 = m.effectiveWithMode(rig.code);
    assert(partConfig(s4.scroll_v, s4.parts[PART_V_TILT], Mix()).sign == 1);

    // an invalid value is refused and nothing is written, the channel is not split for it
    PtRig fresh;
    assert(!setPartTuning(fresh.m, fresh.code, "h_tilt", "CURVE", "abc", &err) && !err.empty());
    assert(!setPartTuning(fresh.m, fresh.code, "h_tilt", "SIGN", "7", &err));
    assert(ptSection(fresh.m, fresh.code).empty());
    assert(!setPartTuning(fresh.m, fresh.code, "twist", "CURVE", "2", &err) && !setPartTuning(fresh.m, fresh.code, "h_tilt", "NOPE", "2", &err));
    assert(ptSection(fresh.m, fresh.code).empty());
}

static void testSplitKeepsEveryMotionAsItWas() {
    // Each of the four motions on its own does exactly what it did before the split, for a profile, a device and Global.
    for (int which = 0; which < 3; ++which) {
        PtRig rig;
        SettingsModel &m = rig.m;
        const size_t section = which == 0 ? rig.code : which == 1 ? rig.dev : 0;
        std::string err;
        assert(m.setValue(0, "SCROLL_V_CURVE", "1.5", &err) && m.setValue(0, "SCROLL_H_SENS", "6", &err));
        const SettingsModel before = m;
        const struct { Axis axis; bool h; } probes[4] = {{AX_Z, false}, {AX_RX, false}, {AX_X, true}, {AX_RZ, true}};
        int want[4];
        for (int i = 0; i < 4; ++i) want[i] = ptRun(before, section, probes[i].axis, 250, probes[i].h);
        for (int i = 0; i < 4; ++i) assert(want[i] != 0);
        // edit one value of each of the four motions, one after the other
        assert(setPartTuning(m, section, "v_push", "SMOOTH_MS", "0", &err));
        assert(setPartTuning(m, section, "v_tilt", "DEADZONE", "30", &err));
        assert(setPartTuning(m, section, "h_push", "CURVE", "1", &err));
        assert(setPartTuning(m, section, "h_tilt", "SENS", "6", &err));
        // values equal to the inherited ones: every motion still does what it did
        for (int i = 0; i < 4; ++i) assert(ptRun(m, section, probes[i].axis, 250, probes[i].h) == want[i]);
    }
}

static void testChannelActionIsKept() {
    // the channel has an action of its own (keys): both parts get it, a part action "default" would have replaced it
    PtRig rig;
    SettingsModel &m = rig.m;
    std::string err;
    assert(m.setValue(0, "V_ACTION", "keys:up,down", &err));
    int tapsBefore = 0, tapsAfter = 0;
    ptRun(m, rig.code, AX_Z, 300, false, &tapsBefore);
    assert(tapsBefore > 0);
    assert(setPartTuning(m, rig.code, "v_tilt", "SIGN", "-1", &err));
    const std::string text = ptSection(m, rig.code);
    assert(ptHas(text, "V_PUSH_ACTION=keys:up,down;") && ptHas(text, "V_TILT_ACTION=keys:up,down;"));
    ptRun(m, rig.code, AX_Z, 300, false, &tapsAfter);
    assert(tapsAfter == tapsBefore);
    assert(motion_edit_detail::effectiveAction(m, rig.code, "v_push").type == ChannelActionType::Keys && motion_edit_detail::effectiveAction(m, rig.code, "v_tilt").type == ChannelActionType::Keys);
}

static void testJoystickModeKeepsTheChannel() {
    // a joystick mode that sets the channel action beats the part actions of Global and a device profile: the split cannot be made
    // there (and nothing is written); a program profile has the last word
    PtRig rig;
    SettingsModel &m = rig.m;
    ModeSet ms = loadModes("/nonexistent-dir-for-test", nullptr);
    ModeDef d;
    d.id = "keysmode";
    d.label = "Keys";
    d.v.type = ChannelActionType::Keys;
    d.v.neg = Key::Up;
    d.v.pos = Key::Down;
    ms.modes.push_back(d);
    m.setModes(ms, "");
    std::string err;
    assert(m.setValue(rig.dev, "MODE", "keysmode", &err) && m.setValue(rig.code, "MODE", "keysmode", &err));
    const std::string devBefore = ptSection(m, rig.dev);
    assert(!setPartTuning(m, rig.dev, "v_tilt", "SIGN", "-1", &err));
    assert(ptHas(err, "joystick mode") && ptSection(m, rig.dev) == devBefore);
    assert(setPartTuning(m, rig.code, "v_tilt", "SIGN", "-1", &err));
    assert(ptHas(ptSection(m, rig.code), "V_PUSH_ACTION=keys:up,down;"));
    assert(m.effectiveWithMode(rig.code).parts[PART_V_TILT].cfg.sign == -1);
}

static void testStaleKeysAreNotSwitchedOn() {
    // keys of a part that did not act (the channel was not split) are pinned to what the motion really does
    PtRig rig;
    SettingsModel &m = rig.m;
    std::string err;
    assert(m.setValue(0, "SCROLL_V_TILT_CURVE", "3", &err) && m.setValue(0, "SCROLL_V_TILT_SIGN", "-1", &err));
    assert(m.setValue(0, "SCROLL_V_PUSH_TRIGGER", "20", &err));
    const int tiltBefore = ptRun(m, rig.code, AX_RX, 300);
    assert(tiltBefore > 0);  // unsplit: the stale values do nothing
    PartTuningView v = partTuningView(m, rig.code, "v_tilt", IdentityStrings());
    assert(!v.split && ptField(v, "SIGN")->value == "1" && ptField(v, "CURVE")->value == "1" && ptHas(v.note, "share one tuning for now"));
    assert(setPartTuning(m, rig.code, "v_push", "SENS", "7", &err));
    assert(ptRun(m, rig.code, AX_RX, 300) == tiltBefore);
    const Settings s = m.effectiveWithMode(rig.code);
    assert(partConfig(s.scroll_v, s.parts[PART_V_TILT], Mix()).sign == 1 && partConfig(s.scroll_v, s.parts[PART_V_TILT], Mix()).curve_exp == 1.0);
    // the key function values of an unsplit channel run on the push slot: the tilt gets them
    assert((s.key_tuning[KM_V_TILT].set & KeyTuning::F_TRIGGER) && s.key_tuning[KM_V_TILT].trigger == 20.0);
    v = partTuningView(m, rig.code, "v_tilt", IdentityStrings());
    assert(v.split);
}

static void testKeyFunctionValuesFollow() {
    PtRig rig;
    SettingsModel &m = rig.m;
    std::string err;
    assert(m.setValue(rig.code, "SCROLL_V_PUSH_TRIGGER", "25", &err));
    assert(keyEngineSettings(m, rig.code, "v_tilt").key_tuning[KM_V_TILT].trigger == 25.0);  // what the tilt really uses
    assert(splitChannelFor(m, rig.code, "v_tilt", "SCROLL_V_TILT_TRIGGER", &err));
    assert(m.effectiveWithMode(rig.code).parts[PART_V_TILT].set == 0);
    assert(ptHas(ptSection(m, rig.code), "V_TILT_ACTION=default;") && ptHas(ptSection(m, rig.code), "V_PUSH_ACTION=default;"));
    // the edited key is left to the caller; a second call is a no-op
    assert(!ptHas(ptSection(m, rig.code), "SCROLL_V_TILT_TRIGGER"));
    const std::string once = ptSection(m, rig.code);
    assert(splitChannelFor(m, rig.code, "v_tilt", "SCROLL_V_TILT_TRIGGER", &err) && ptSection(m, rig.code) == once);
    assert(splitChannelFor(m, rig.code, "twist", "", &err) && ptSection(m, rig.code) == once);
    // a motion that was edited by the key function box: pin of the other motions' values
    PtRig two;
    assert(two.m.setValue(two.code, "SCROLL_H_PUSH_TRIGGER", "12", &err));
    assert(splitChannelFor(two.m, two.code, "h_push", "SCROLL_H_PUSH_TRIGGER", &err));
    assert(two.m.effectiveWithMode(two.code).key_tuning[KM_H_TILT].trigger == 12.0);  // the tilt did run on the push slot
}

static void testStateOfAMotion() {
    PtRig rig;
    SettingsModel &m = rig.m;
    IdentityStrings id;
    std::string err;
    MotionMemory mem;
    // tuning of the whole channel is not "this motion's": the state stays inherited
    assert(m.setValue(rig.code, "SCROLL_V_SENS", "9", &err));
    assert(motionEdit(m, rig.code, "v_tilt", id).state == MotionState::Inherited && !motionEdit(m, rig.code, "v_tilt", id).canInherit);
    assert(setPartTuning(m, rig.code, "v_tilt", "CURVE", "2", &err));
    MotionEdit e = motionEdit(m, rig.code, "v_tilt", id);
    assert(e.state == MotionState::SetHere && e.canInherit);
    assert(motionEdit(m, rig.code, "v_push", id).state == MotionState::SetHere);  // its action key is set (the split)
    // Use the inherited setting removes what the profile set for the tilt: its action and its tuning; the channel's keys stay
    assert(inheritMotion(m, rig.code, "v_tilt", &mem));
    const std::string text = ptSection(m, rig.code);
    assert(!ptHas(text, "SCROLL_V_TILT_") && !ptHas(text, "V_TILT_ACTION") && ptHas(text, "SCROLL_V_SENS=9;"));
    // a sensitivity 0 of one motion switches that motion off, not the other one
    assert(setPartTuning(m, rig.code, "h_tilt", "SENS", "0", &err));
    assert(!motionEdit(m, rig.code, "h_tilt", id).on && motionEdit(m, rig.code, "h_push", id).on);
    assert(switchOnMotion(m, rig.code, "h_tilt", &mem, &err));
    assert(motionEdit(m, rig.code, "h_tilt", id).on && m.effectiveWithMode(rig.code).parts[PART_H_TILT].cfg.sensitivity > 0);
}

static void testGlobalAndDevice() {
    for (size_t pick = 0; pick < 2; ++pick) {
        PtRig rig;
        SettingsModel &m = rig.m;
        const size_t section = pick == 0 ? 0 : rig.dev;
        IdentityStrings id;
        std::string err;
        assert(setPartTuning(m, section, "h_push", "CURVE", "2.5", &err));
        assert(ptHas(ptSection(m, section), "SCROLL_H_PUSH_CURVE=2.5;") && !ptHas(ptSection(m, section), "SCROLL_H_CURVE"));
        const PartTuningView v = partTuningView(m, section, "h_push", id);
        assert(ptField(v, "CURVE")->own && ptHas(ptField(v, "CURVE")->words, section == 0 ? "Set in Global for this motion only" : "Set in this profile for this motion only"));
        // a program profile below it inherits the value of the motion from there, in words, and the tilt does not
        const PartTuningView vc = partTuningView(m, rig.code, "h_push", id);
        assert(section == 0 ? ptHas(ptField(vc, "CURVE")->words, "Inherited from Global (this motion only): 2.5") : !ptField(vc, "CURVE")->own);
        assert(partTuningView(m, rig.code, "h_tilt", id).fields[3].value == "1");
        // the last value goes: the split that was only there for it goes too, the profile is plain again
        assert(clearPartTuning(m, section, "h_push", "CURVE") && ptSection(m, section).empty());
    }
    {   // a split that Global makes is not touched by the profile that tuned a motion and lets go of it again
        PtRig rig;
        SettingsModel &m = rig.m;
        std::string err;
        assert(m.setValue(0, "V_TILT_ACTION", "none", &err));
        assert(setPartTuning(m, rig.code, "v_push", "CURVE", "2", &err));
        assert(ptSection(m, rig.code) == "SCROLL_V_PUSH_CURVE=2;");  // already split: no action keys
        assert(clearPartTuning(m, rig.code, "v_push", "CURVE") && ptSection(m, rig.code).empty());
    }
}

static void testModeFormSplit() {
    const Settings base;
    ModeForm f;
    std::string err;
    f.setOverride("SCROLL_V_TILT_CURVE", "3");  // a stale key of the tilt: it did not act, the channel is not split
    splitForEdit(f, base, Motion::VPush, "SCROLL_V_PUSH_SENS");
    assert(f.vSplit.active() && f.vSplit.pushSet && f.vSplit.tiltSet && f.vSplit.push.type == ChannelActionType::Default);
    assert(*f.overrideValue("SCROLL_V_TILT_CURVE") == "1");  // pinned to what the tilt did
    assert(!f.overrideValue("SCROLL_V_PUSH_SENS") && !f.hSplit.active());
    const ModeForm once = f;
    splitForEdit(f, base, Motion::VTilt, "SCROLL_V_TILT_SENS");
    splitForEdit(f, base, Motion::Twist, "ZOOM_SENS");
    splitForEdit(f, base, Motion::Lift, "LIFT_SENS");
    assert(f == once);
    // a channel with an action of its own keeps it on both parts
    ModeForm k;
    k.h.type = ChannelActionType::Keys;
    k.h.neg = "left";
    k.h.pos = "right";
    splitForEdit(k, base, Motion::HTilt, "SCROLL_H_TILT_CURVE");
    assert(k.hSplit.push.type == ChannelActionType::Keys && k.hSplit.tilt.type == ChannelActionType::Keys && k.hSplit.tilt.pos == "right");
    // the key function values of the push slot are the tilt's too
    ModeForm t;
    t.setOverride("SCROLL_V_PUSH_TRIGGER", "18");
    splitForEdit(t, base, Motion::VTilt, "SCROLL_V_TILT_KEY_RATE_MIN");
    assert(*t.overrideValue("SCROLL_V_TILT_TRIGGER") == "18");
}

int main() {
    testPartEditWritesOnlyThePart();
    testSplitKeepsEveryMotionAsItWas();
    testChannelActionIsKept();
    testJoystickModeKeepsTheChannel();
    testStaleKeysAreNotSwitchedOn();
    testKeyFunctionValuesFollow();
    testStateOfAMotion();
    testGlobalAndDevice();
    testModeFormSplit();
    std::puts("test_ui_part_tuning OK");
    return 0;
}
