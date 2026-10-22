// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Help of the visual joystick mode editor (issue #143): the live sentence with real rates, the "?" topics, the
// templates, the hints that name the cause and the guided tour.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "testutil.hpp"
#include "ui/model/motion_help_model.hpp"
#include "ui/model/tour_model.hpp"
#include "ui/model/ui_progress.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct English : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
ChannelForm keys(const char *neg, const char *pos) { return templateKeys(ChannelActionType::Keys, neg, pos); }
}  // namespace

static void testSentence(const StringProvider &s) {
    const Settings base;
    ModeForm f;
    // default channel: as the profile says
    assert(has(motionSentence(f, base, Motion::VPush, s), "Vertical push: works as set in the profile"));
    f.v = templateType(ChannelActionType::WheelV);
    const std::string wheel = motionSentence(f, base, Motion::VPush, s);
    assert(has(wheel, "scrolls up and down, about ") && has(wheel, "notches a second at full deflection"));
    // keys: names the keys and a rate, which follows the sensitivity
    f.v = keys("pageup", "pagedown");
    const std::string a = motionSentence(f, base, Motion::VTilt, s);
    assert(has(a, "Vertical tilt: presses Page up or Page down, about ") && has(a, " times a second at full deflection"));
    ModeForm hot = f;
    hot.setOverride("SCROLL_V_SENS", "20");
    const std::string b = motionSentence(hot, base, Motion::VTilt, s);
    assert(a != b);
    const double ra = help_detail::rateAt(base.scroll_v, Motion::VTilt, kFullDeflection);
    ModeForm cold = f;
    cold.setOverride("SCROLL_V_SENS", "1");
    ChannelCfg cc;
    assert(help_detail::configOf(cold, base, Motion::VTilt, &cc) && cc.sensitivity == 1);
    assert(help_detail::rateAt(cc, Motion::VTilt, kFullDeflection) < ra);
    // a split part uses its own tuning
    ModeForm sp = f;
    assert(assign(sp, base, Motion::VPush, templateType(ChannelActionType::WheelV)).ok);
    sp.setOverride("SCROLL_V_PUSH_SENS", "1");
    ChannelCfg part;
    assert(help_detail::configOf(sp, base, Motion::VPush, &part) && part.sensitivity == 1);
    assert(help_detail::configOf(sp, base, Motion::VTilt, &part) && part.sensitivity == base.scroll_v.sensitivity);
    // live: the key follows the direction and the rate follows the deflection
    const std::string down = motionSentence(f, base, Motion::VTilt, s, 300.0), up = motionSentence(f, base, Motion::VTilt, s, -300.0);
    assert(has(down, ", now: Page down, ") && has(up, ", now: Page up, "));
    assert(has(motionSentence(f, base, Motion::VTilt, s, 0.0), ", now: at rest"));
    // step, off, zoom, reversed, lift, custom mix
    f.h = templateKeys(ChannelActionType::Step, "left", "right");
    assert(has(motionSentence(f, base, Motion::HPush, s), "taps Left or Right once per push"));
    f.h = templateType(ChannelActionType::Off);
    assert(has(motionSentence(f, base, Motion::HPush, s), "does nothing"));
    f.twist = templateType(ChannelActionType::Zoom);
    assert(has(motionSentence(f, base, Motion::Twist, s), "zooms"));
    assert(setReversed(f, base, Motion::Twist, true).ok && has(motionSentence(f, base, Motion::Twist, s), "(direction reversed)"));
    assert(has(motionSentence(f, base, Motion::Lift, s), "not used"));
    assert(setLift(f, base, MotionChannel::V, 1.0).ok && has(motionSentence(f, base, Motion::Lift, s), "false triggers"));
    ModeForm custom;
    custom.setOverride("SCROLL_V_MIX", "z");
    assert(has(motionSentence(custom, base, Motion::VTilt, s), "no channel uses this motion"));
    // buttons
    ModeForm bf;
    bf.setButton(2, "key:space");
    assert(buttonSentence(bf, 2, s) == "Button 2: Space");
    assert(has(buttonSentence(bf, 3, s), "the profile's binding applies"));
}

static void testTopics(const StringProvider &s) {
    for (Motion m : allMotions()) {
        const HelpTopic t = helpTopic(std::string("motion:") + motionId(m), s);
        assert(!t.title.empty() && has(t.body, "axis ") && has(t.body, "Example:") && has(t.body, "In the file:"));
    }
    assert(has(helpTopic("motion:v_push", s).body, "V_PUSH_ACTION") && has(helpTopic("motion:twist", s).body, "TWIST_ACTION"));
    for (const char *id : {"action:default", "action:none", "action:wheel_v", "action:wheel_h", "action:zoom", "action:keys", "action:step", "reverse",
                           "tuning", "button", "picture"}) {
        const HelpTopic t = helpTopic(id, s);
        assert(!t.title.empty() && !t.body.empty());
    }
    assert(has(helpTopic("reverse", s).body, "SIGN") && has(helpTopic("action:keys", s).body, "keys:"));
    assert(helpTopic("nope", s).title.empty());
}

static void testTemplates(const StringProvider &s) {
    const Settings base;
    const std::vector<MotionTemplate> all = motionTemplates(s);
    assert(all.size() == 5);
    for (const MotionTemplate &t : all) {
        assert(!t.id.empty() && !t.title.empty() && !t.description.empty());
        ModeForm f;
        f.name = "Keep";
        f.setButton(1, "key:space");
        f.setOverride("SCROLL_V_SENS", "3");
        assign(f, base, Motion::VTilt, templateType(ChannelActionType::WheelH));  // split, to see it undone
        applyTemplate(f, t);
        assert(!f.vSplit.active() && !f.hSplit.active() && f.v == t.v && f.h == t.h && f.twist == t.twist);
        assert(f.name == "Keep" && f.button(1) && f.overrideValue("SCROLL_V_SENS"));
        // the result is a valid mode file
        ModeEditor ed;
        const std::string root = tu::makeTempDir("dofscroll-motion-help");
        const std::string dir = root + "/modes";
        assert(fs::mkdirsPrivate(dir, nullptr));
        std::string err;
        assert(fs::writeFileReplace(dir + "/t.conf", "NAME = T\n", &err));
        const ModeSet ms = loadModes(dir, nullptr);
        assert(ms.find("t") && ed.open(dir + "/t.conf", *ms.find("t"), &err));
        applyTemplate(ed.form(), t);
        assert(ed.validate().empty() && ed.save().ok);
        tu::removeAll(root);
    }
}

static void testHints(const StringProvider &s) {
    const Settings base;
    auto find = [](const std::vector<MotionHint> &h, const std::string &id) -> const MotionHint * {
        for (const MotionHint &x : h)
            if (x.id == id) return &x;
        return nullptr;
    };
    ModeForm f;
    assert(motionHints(f, base, 0, s).empty());  // a plain mode has nothing to say
    // a motion that does nothing
    f.h = templateType(ChannelActionType::Off);
    {
        const auto h = motionHints(f, base, 0, s);
        const MotionHint *x = find(h, "off:h_push");
        assert(x && x->text == "Horizontal push does nothing in this joystick mode" && x->fixLabel == "Assign it" && x->fixId == "select:h_push");
        assert(find(h, "off:h_tilt"));
    }
    // two motions with the same key
    f.h = keys("left", "right");
    f.v = keys("up", "left");
    {
        const auto h = motionHints(f, base, 0, s);
        bool shared = false;
        for (const MotionHint &x : h) shared = shared || (x.id.rfind("keys:", 0) == 0 && has(x.text, "both press Left"));
        assert(shared);
    }
    // push and tilt of an unsplit channel are one action, never a conflict
    ModeForm one;
    one.v = keys("up", "down");
    for (const MotionHint &x : motionHints(one, base, 0, s)) assert(x.id.rfind("keys:", 0) != 0);
    // a channel mix that differs from the profile's, but not the lift term alone
    ModeForm mix;
    mix.setOverride("SCROLL_V_MIX", "z");
    assert(find(motionHints(mix, base, 0, s), "mix:SCROLL_V_MIX"));
    ModeForm lift;
    assert(setLift(lift, base, MotionChannel::V, 1.0).ok);
    assert(!find(motionHints(lift, base, 0, s), "mix:SCROLL_V_MIX"));
    // a button the device does not have
    ModeForm b;
    b.setButton(20, "key:space");
    assert(!find(motionHints(b, base, 0, s), "button:20"));  // device unknown: nothing to compare with
    const std::vector<MotionHint> withDevice = motionHints(b, base, 15, s);  // find() points into it: keep it alive
    const MotionHint *bh = find(withDevice, "button:20");
    assert(bh && has(bh->text, "only 15 buttons") && bh->fixId == "unbind:20");
}

static void testTour(const StringProvider &s) {
    // with a device: the real movement is needed until "Click it instead"
    TourModel t(true);
    assert(t.active() && t.step() == TourStep::Move && t.number() == 1 && t.waitsForDevice() && t.canClickInstead());
    assert(has(t.text(s), "Move the cap"));
    assert(!t.on(TourEvent::MotionClicked) && t.step() == TourStep::Move);
    t.chooseClick();
    assert(!t.waitsForDevice() && has(t.text(s), "Click a motion") && t.on(TourEvent::MotionClicked) && t.step() == TourStep::Pick);
    assert(!t.on(TourEvent::ButtonDetected));  // wrong event for the step
    assert(t.on(TourEvent::ActionAssigned) && t.step() == TourStep::Press && t.waitsForDevice());
    assert(t.on(TourEvent::ButtonDetected) && t.step() == TourStep::Try && !t.waitsForDevice());
    assert(!t.finished() && t.on(TourEvent::Tried) && t.finished() && t.number() == 4 && !t.active() && !t.on(TourEvent::Tried));
    // without a device everything is a click
    TourModel n(false);
    assert(!n.waitsForDevice() && !n.canClickInstead() && n.on(TourEvent::MotionClicked) && n.on(TourEvent::ActionAssigned) &&
           n.on(TourEvent::ButtonClicked) && n.step() == TourStep::Try);
    // the daemon came while the tour ran: the next step waits for the real thing
    TourModel m(false);
    m.next();
    m.next();
    m.setDevice(true);
    assert(m.step() == TourStep::Press && m.waitsForDevice());
    // dismissing, restarting
    m.dismiss();
    assert(m.dismissed() && !m.active() && !m.on(TourEvent::ButtonDetected));
    m.restart();
    assert(m.active() && m.step() == TourStep::Move);
}

static void testProgress() {
    const std::string root = tu::makeTempDir("dofscroll-tour-progress");
    const std::string path = root + "/ui-progress.conf";
    assert(!loadUiProgress(path).motionTourSeen);
    std::string err;
    assert(markMotionTourSeen(path, &err) && loadUiProgress(path).motionTourSeen);
    assert(!loadUiProgress(path).axisAssistantApplied);  // independent
    assert(markAxisAssistantApplied(path, &err) && loadUiProgress(path).motionTourSeen && loadUiProgress(path).axisAssistantApplied);
    UiProgress p;
    p.motionTourSeen = true;
    assert(parseUiProgress(serializeUiProgress(p)) == p);
    tu::removeAll(root);
}

int main() {
    English s;
    testSentence(s);
    testTopics(s);
    testTemplates(s);
    testHints(s);
    testTour(s);
    testProgress();
    std::puts("test_ui_motion_help OK");
    return 0;
}
