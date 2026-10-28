// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The analog / digital choice and the rate and trigger fields of a key function (stage 3 of #224,
// ui/model/key_engine_model.hpp): when analog is allowed and why not, the check box that only switches keys: and step:, the keys
// of the motion in a profile and in a joystick mode file (validated like everywhere), lift as a motion of the mode editor.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "config.hpp"
#include "testutil.hpp"
#include "ui/model/key_engine_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

static ChannelForm form(ChannelActionType t, const char *neg, const char *pos) {
    ChannelForm f;
    f.type = t;
    f.neg = neg;
    f.pos = pos;
    return f;
}
static bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

static void testNames() {
    assert(keyEngineKey("v_push", KeyField::RateMin) == "SCROLL_V_PUSH_KEY_RATE_MIN");
    assert(keyEngineKey("v_tilt", KeyField::RateMax) == "SCROLL_V_TILT_KEY_RATE_MAX");
    assert(keyEngineKey("h_push", KeyField::Trigger) == "SCROLL_H_PUSH_TRIGGER");
    assert(keyEngineKey("h_tilt", KeyField::Trigger) == "SCROLL_H_TILT_TRIGGER");
    assert(keyEngineKey("twist", KeyField::RateMin) == "ZOOM_KEY_RATE_MIN");
    assert(keyEngineKey("lift", KeyField::Trigger) == "LIFT_TRIGGER");
    assert(keyEngineKey("nonsense", KeyField::Trigger).empty());
    // every name is a key the config accepts
    for (const char *id : {"v_push", "v_tilt", "h_push", "h_tilt", "twist", "lift"})
        for (KeyField f : {KeyField::RateMin, KeyField::RateMax, KeyField::Trigger}) {
            std::string err;
            assert(checkKeyField(keyEngineKey(id, f), "5", &err));
        }
}

static void testAnalogChoice() {
    IdentityStrings s;
    // not a key function: no box
    assert(!analogChoice("v_push", form(ChannelActionType::WheelV, "up", "down"), s).shown);
    // tier A: analog, allowed
    AnalogChoice c = analogChoice("v_push", form(ChannelActionType::Keys, "pageup", "pagedown"), s);
    assert(c.shown && c.analog && c.allowed && c.reason.empty());
    c = analogChoice("h_tilt", form(ChannelActionType::Step, "ctrl+pageup", "ctrl+pagedown"), s);
    assert(c.shown && !c.analog && c.allowed);  // digital now, analog can be chosen
    // tier B: digital only, the box is greyed with the reason
    c = analogChoice("v_push", form(ChannelActionType::Step, "j", "l"), s);
    assert(c.shown && !c.analog && !c.allowed && !c.reason.empty());
    c = analogChoice("twist", form(ChannelActionType::Step, "alt+tab", "alt+shift+tab"), s);
    assert(!c.allowed && c.reason.find("digitally") != std::string::npos);
    c = analogChoice("v_push", form(ChannelActionType::Step, "up", "enter"), s);
    assert(!c.allowed);  // one risky side is enough
    // a none side is no key
    c = analogChoice("v_push", form(ChannelActionType::Step, "none", "pagedown"), s);
    assert(c.allowed);
    c = analogChoice("v_push", form(ChannelActionType::Step, "none", "a"), s);
    assert(!c.allowed);
    // the lift is never analog, whatever the keys
    c = analogChoice("lift", form(ChannelActionType::Step, "up", "down"), s);
    assert(c.shown && !c.analog && !c.allowed && c.reason.find("grab") != std::string::npos);
}

static void testSwitch() {
    ChannelForm f = form(ChannelActionType::Keys, "ctrl+pageup", "ctrl+pagedown");
    assert(setAnalog(&f, false) && f.type == ChannelActionType::Step && f.text() == "step:ctrl+pageup,ctrl+pagedown");
    assert(setAnalog(&f, true) && f.type == ChannelActionType::Keys && f.text() == "keys:ctrl+pageup,ctrl+pagedown");
    ChannelForm w;
    w.type = ChannelActionType::WheelV;
    assert(!setAnalog(&w, true) && w.type == ChannelActionType::WheelV);
    // a risky chord switched to analog is refused by the same parser as the file loader
    ChannelForm r = form(ChannelActionType::Step, "j", "l");
    assert(setAnalog(&r, true));
    ChannelAction a;
    std::string err;
    assert(!parseChannelAction(r.text(), &a, &err) && err == "this key can only be used digitally, use step:");
    // none per side in the text
    ChannelForm n = form(ChannelActionType::Step, "none", "down");
    assert(n.text() == "step:none,down" && parseChannelAction(n.text(), &a, nullptr) && a.negNone);
    ChannelForm back = channelFormOf(a);
    assert(back.neg == "none" && back.pos == "down" && back == n);
}

static void testFields() {
    // the UI defaults of a new analog function: only when the motion sets no rate yet
    std::string have;
    auto get = [&](const std::string &k) { return k == "SCROLL_V_PUSH_KEY_RATE_MAX" ? have : std::string(); };
    auto v = analogStartValues("v_push", get);
    assert(v.size() == 2 && v[0].first == "SCROLL_V_PUSH_KEY_RATE_MIN" && v[0].second == "3" && v[1].first == "SCROLL_V_PUSH_KEY_RATE_MAX" && v[1].second == "25");
    have = "9";
    assert(analogStartValues("v_push", get).empty());
    assert(analogStartValues("nonsense", get).empty());
    // what a field accepts
    std::string err;
    assert(checkKeyField("ZOOM_KEY_RATE_MAX", "12.5", &err));
    assert(!checkKeyField("ZOOM_KEY_RATE_MAX", "-1", &err) && !err.empty());
    assert(!checkKeyField("ZOOM_KEY_RATE_MAX", "fast", &err) && err == "number expected");
    assert(checkKeyField("LIFT_TRIGGER", "40", &err));
    // the trigger in words: about N percent of full deflection (sens 5, dead zone 30: 20 steps/s is raw 110 of 350)
    Settings s;
    assert(triggerPercent(s, "v_push", 20.0) == 31);
    s.scroll_v.sensitivity = 0;
    assert(triggerPercent(s, "v_push", 20.0) == 0);
    assert(triggerPercent(Settings(), "nonsense", 20.0) == 0 && triggerPercent(Settings(), "v_push", 0.0) == 0);
    IdentityStrings is;
    const KeyEngineWords w = keyEngineWords(false, Settings(), "v_push", is);
    assert(has(w.trigger, "31 %") && has(w.mode, "Digital"));
    const KeyEngineWords a = keyEngineWords(true, Settings(), "v_push", is);
    assert(has(a.mode, "Analog") && has(a.defaults, "3 to 25"));
    // effectiveTrigger: the motion's own, else STEP_TRIGGER
    Settings t;
    assert(effectiveTrigger(t, "v_push") == 20.0);
    applyKey(t, "SCROLL_V_PUSH_TRIGGER", "35", nullptr);
    assert(effectiveTrigger(t, "v_push") == 35.0 && effectiveTrigger(t, "h_push") == 20.0);
}

static void testProfile() {
    useFreshDefaultOf("linux");
    const std::string root = tu::makeTempDir("dofscroll-key-engine-ui");
    ProfileSet ps;
    const size_t app = ps.findOrAdd(Section::APP, "player");
    SettingsModel m;
    m.setProfiles(ps, "");
    std::string err;
    // the keys of a motion are valid in every section and stored as the keys of the file
    assert(m.setValue(app, "V_TILT_ACTION", "keys:pageup,pagedown", &err));
    assert(m.setValue(app, "SCROLL_V_TILT_KEY_RATE_MAX", "8", &err));
    assert(m.setValue(app, "SCROLL_V_TILT_TRIGGER", "30", &err));
    assert(m.setValue(0, "ZOOM_KEY_RATE_MIN", "2", &err));
    assert(!m.setValue(app, "SCROLL_V_TILT_KEY_RATE_MAX", "-3", &err) && !err.empty());
    assert(!m.setValue(app, "SCROLL_V_TILT_TRIGGER", "x", &err));
    const Settings eff = m.effectiveWithMode(app);
    assert(eff.key_tuning[KM_V_TILT].rate_max == 8.0 && eff.key_tuning[KM_V_TILT].trigger == 30.0 && eff.key_tuning[KM_TWIST].rate_min == 2.0);
    assert(m.clearOverride(app, "SCROLL_V_TILT_KEY_RATE_MAX"));
    assert(m.effectiveWithMode(app).key_tuning[KM_V_TILT].rate_max == 0.0);
    // the analog action of a tier B key is refused by setMotionAction (the same parser)
    ChannelForm bad = form(ChannelActionType::Keys, "j", "l");
    assert(!setMotionAction(m, app, "twist", bad, &err) && err == "this key can only be used digitally, use step:");
    ChannelForm ok = form(ChannelActionType::Step, "j", "l");
    assert(setMotionAction(m, app, "twist", ok, &err));
    // a tilt of a channel that is not split uses the push keys
    assert(motion_edit_detail::keyTuningMotion(m, app, "v_tilt") == "v_tilt");  // split by V_TILT_ACTION
    assert(motion_edit_detail::keyTuningMotion(m, app, "h_tilt") == "h_push");  // H is not split
    assert(motion_edit_detail::keyTuningMotion(m, app, "twist") == "twist");
    tu::removeAll(root);
}

static const char *kMode =
    "NAME = Demo\n"
    "V_ACTION = keys:up,down\n"
    "LIFT_ACTION = step:volume_up,volume_down\n"
    "SCROLL_V_PUSH_KEY_RATE_MAX = 12\n";

static void testModeEditor() {
    const std::string dir = tu::makeTempDir("dofscroll-key-engine-mode");
    std::ofstream(dir + "/demo.conf") << kMode;
    const ModeSet ms = loadModes(dir, nullptr);
    const ModeDef *def = ms.find("demo");
    assert(def && def->lift.type == ChannelActionType::Step);
    ModeEditor ed;
    std::string err;
    assert(ed.open(dir + "/demo.conf", *def, &err));
    // lift is a motion of the form now (it was read from LIFT_ACTION)
    assert(ed.form().lift.type == ChannelActionType::Step && ed.form().lift.neg == "volume_up" && ed.form().lift.pos == "volume_down");
    assert(ed.validate().empty() && !ed.dirty());
    assert(ed.compose() == kMode);  // nothing changes without an edit
    // the label of the picture shows the lift action
    IdentityStrings s;
    const MotionLabel l = motionLabel(ed.form(), Settings(), Motion::Lift, s);
    assert(l.kind == MotionKind::Active && l.chip == "Taps" && l.tile.find("Volume") != std::string::npos);
    // change the lift, switch the channel to digital and set rate keys: written as the keys of the file
    ed.form().lift = form(ChannelActionType::Step, "none", "pagedown");
    ed.form().v.type = ChannelActionType::Step;
    ed.form().setOverride("SCROLL_V_PUSH_KEY_RATE_MIN", "3");
    ed.form().setOverride("SCROLL_V_PUSH_KEY_RATE_MAX", "");
    ed.form().setOverride("SCROLL_V_PUSH_TRIGGER", "25");
    const std::string text = ed.compose();
    assert(has(text, "V_ACTION = step:up,down") && has(text, "LIFT_ACTION = step:none,pagedown") && has(text, "SCROLL_V_PUSH_KEY_RATE_MIN = 3") &&
           has(text, "SCROLL_V_PUSH_TRIGGER = 25") && !has(text, "KEY_RATE_MAX"));
    assert(ed.validate().empty());
    auto r = ed.save();
    assert(r.ok);
    {
        const ModeSet again = loadModes(dir, nullptr);
        const ModeDef *m = again.find("demo");
        assert(m && m->lift.posNone == false && m->lift.negNone && m->v.type == ChannelActionType::Step);
        Settings probe;
        for (const auto &kv : m->overrides) assert(applyKey(probe, kv.first, kv.second, nullptr));
        assert(probe.key_tuning[KM_V_PUSH].rate_min == 3.0 && probe.key_tuning[KM_V_PUSH].trigger == 25.0);
    }
    // an analog lift is a problem of the field, never written
    ed.form().lift = form(ChannelActionType::Keys, "up", "down");
    bool lift = false;
    for (const EditProblem &p : ed.validate()) lift = lift || p.field == "lift";
    assert(lift && !ed.save().ok && has(get(dir + "/demo.conf"), "LIFT_ACTION = step:none,pagedown"));
    // an analog risky key is a problem of its channel, an invalid rate a problem of its override
    ed.form().lift = form(ChannelActionType::Step, "up", "down");
    ed.form().v = form(ChannelActionType::Keys, "j", "l");
    ed.form().setOverride("ZOOM_KEY_RATE_MAX", "fast");
    bool v = false, o = false;
    std::string vmsg;
    for (const EditProblem &p : ed.validate()) {
        if (p.field == "v") { v = true; vmsg = p.message; }
        o = o || p.field == "override:ZOOM_KEY_RATE_MAX";
    }
    assert(v && o && vmsg == "this key can only be used digitally, use step:");
    // the lift back to "default" removes the key from the file
    ed.form().v = form(ChannelActionType::Keys, "up", "down");
    ed.form().setOverride("ZOOM_KEY_RATE_MAX", "");
    ed.form().lift = ChannelForm();
    r = ed.save();
    assert(r.ok && !has(get(dir + "/demo.conf"), "LIFT_ACTION"));
    tu::removeAll(dir);
}

int main() {
    testNames();
    testAnalogChoice();
    testSwitch();
    testFields();
    testProfile();
    testModeEditor();
    std::puts("test_ui_key_engine: all passed");
    return 0;
}
