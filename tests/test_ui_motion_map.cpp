// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Motion map of the visual joystick mode editor (issue #143): six motions to the keys of a mode file, split
// and unsplit collapse, custom mixes, reverse direction through the existing *_SIGN keys, lift/press as a
// mix term, and the round trip over every built-in and example joystick mode.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/motion_map_model.hpp"

using namespace spn;
using namespace spn::ui;

static ChannelForm keys(const char *neg, const char *pos) {
    ChannelForm c;
    c.type = ChannelActionType::Keys;
    c.neg = neg;
    c.pos = pos;
    return c;
}
static ChannelForm of(ChannelActionType t) {
    ChannelForm c;
    c.type = t;
    return c;
}
static bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }

static void testIds() {
    assert(allMotions().size() == 6);
    for (Motion m : allMotions()) {
        Motion back;
        assert(motionFromId(motionId(m), &back) && back == m);
        assert(motionOfAxis(motionAxis(m)) == m);
    }
    Motion x;
    assert(!motionFromId("nope", &x));
}

static void testSplitAndCollapse() {
    const Settings base;
    ModeForm f;
    // unsplit: all four scroll motions share the channel's action; lift is in no mix
    assert(actionOf(f, base, Motion::VPush) == ChannelForm());
    bool found = true;
    actionOf(f, base, Motion::Lift, &found);
    assert(!found);

    // tilt gets its own action: the channel splits, push keeps what it did
    assert(assign(f, base, Motion::VTilt, keys("pageup", "pagedown")).ok);
    assert(f.vSplit.pushSet && f.vSplit.tiltSet);
    assert(f.vSplit.push == ChannelForm() && f.vSplit.tilt == keys("pageup", "pagedown"));
    assert(actionOf(f, base, Motion::VTilt) == keys("pageup", "pagedown"));
    assert(actionOf(f, base, Motion::VPush) == ChannelForm());
    assert(!f.hSplit.active());

    // push gets the same action: back to the unsplit key
    assert(assign(f, base, Motion::VPush, keys("pageup", "pagedown")).ok);
    assert(!f.vSplit.active() && f.v == keys("pageup", "pagedown"));

    // the same action again changes nothing, also for a split whose parts happen to be equal (hand-written)
    ModeForm g;
    g.vSplit.pushSet = g.vSplit.tiltSet = true;
    g.vSplit.push = g.vSplit.tilt = of(ChannelActionType::Zoom);
    const ModeForm before = g;
    assert(assign(g, base, Motion::VTilt, of(ChannelActionType::Zoom)).ok && g == before);

    // only one split key set: the other part follows the channel, assigning it pins it
    ModeForm h;
    h.h = of(ChannelActionType::WheelH);
    h.hSplit.tiltSet = true;
    h.hSplit.tilt = of(ChannelActionType::Off);
    assert(actionOf(h, base, Motion::HPush) == of(ChannelActionType::WheelH));
    assert(actionOf(h, base, Motion::HTilt) == of(ChannelActionType::Off));
    assert(assign(h, base, Motion::HPush, keys("left", "right")).ok);
    assert(h.hSplit.pushSet && h.hSplit.tiltSet && h.hSplit.tilt == of(ChannelActionType::Off));
    assert(h.h == of(ChannelActionType::WheelH));  // the channel's own action is not touched while split

    // twist
    assert(assign(f, base, Motion::Twist, of(ChannelActionType::WheelV)).ok && f.twist.type == ChannelActionType::WheelV);
    assert(actionOf(f, base, Motion::Twist) == of(ChannelActionType::WheelV));

    // lift has no action of its own
    assert(!assign(f, base, Motion::Lift, of(ChannelActionType::Zoom)).ok);
}

static void testCustomMix() {
    const Settings base;
    ModeForm f;
    f.setOverride("SCROLL_V_MIX", "z");  // tilt is no longer part of the vertical channel
    assert(placeOf(f, base, Motion::VPush).found && placeOf(f, base, Motion::VPush).customMix);
    assert(!placeOf(f, base, Motion::VTilt).found && placeOf(f, base, Motion::VTilt).customMix);
    const AssignResult r = assign(f, base, Motion::VTilt, of(ChannelActionType::Zoom));
    assert(!r.ok && !r.error.empty());
    assert(*f.overrideValue("SCROLL_V_MIX") == "z");  // never touched
    // a mix that moves a motion to another channel: the motion is assigned where it lives
    ModeForm g;
    g.setOverride("ZOOM_MIX", "ry + rx");  // rx is found in V first, so nothing changes for it
    assert(placeOf(g, base, Motion::VTilt).channel == MotionChannel::V && !placeOf(g, base, Motion::VTilt).customMix);
    assert(placeOf(g, base, Motion::Twist).customMix);
    // built-in mixes: nothing is custom
    ModeForm plain;
    for (Motion m : allMotions()) {
        if (m == Motion::Lift) continue;
        assert(placeOf(plain, base, m).found && !placeOf(plain, base, m).customMix);
    }
}

static void testReverse() {
    const Settings base;
    ModeForm f;
    assert(!isReversed(f, base, Motion::VPush));
    assert(setReversed(f, base, Motion::VPush, true).ok);
    assert(*f.overrideValue("SCROLL_V_SIGN") == "-1" && isReversed(f, base, Motion::VPush) && isReversed(f, base, Motion::VTilt));
    assert(setReversed(f, base, Motion::VPush, false).ok);
    assert(f.overrides.empty() && !isReversed(f, base, Motion::VPush));  // no line for the normal direction
    // twist uses ZOOM_SIGN
    assert(setReversed(f, base, Motion::Twist, true).ok && *f.overrideValue("ZOOM_SIGN") == "-1");
    // a split channel reverses one part: SCROLL_V_TILT_SIGN, the other part keeps the channel's sign
    assert(assign(f, base, Motion::VTilt, keys("up", "down")).ok);
    assert(setReversed(f, base, Motion::VTilt, true).ok);
    assert(*f.overrideValue("SCROLL_V_TILT_SIGN") == "-1" && !f.overrideValue("SCROLL_V_PUSH_SIGN"));
    assert(isReversed(f, base, Motion::VTilt) && !isReversed(f, base, Motion::VPush));
    assert(setReversed(f, base, Motion::VTilt, false).ok && !f.overrideValue("SCROLL_V_TILT_SIGN"));
    // the profile already reverses the channel: "not reversed" must say so explicitly
    Settings rev;
    rev.scroll_h.sign = -1;
    ModeForm h;
    assert(isReversed(h, rev, Motion::HPush));
    assert(setReversed(h, rev, Motion::HPush, false).ok && *h.overrideValue("SCROLL_H_SIGN") == "1" && !isReversed(h, rev, Motion::HPush));
    // lift has no direction
    assert(!setReversed(f, base, Motion::Lift, true).ok && !isReversed(f, base, Motion::Lift));
}

static void testLift() {
    const Settings base;
    ModeForm f;
    assert(!liftPlace(f, base).found);
    assert(setLift(f, base, MotionChannel::V, 0.5).ok);
    assert(*f.overrideValue("SCROLL_V_MIX") == "z + rx + 0.5*y");
    const MotionPlace lp = liftPlace(f, base);
    assert(lp.found && lp.channel == MotionChannel::V);
    assert(!placeOf(f, base, Motion::VPush).customMix);  // the lift term does not make the channel "custom"
    assert(actionOf(f, base, Motion::Lift) == ChannelForm());
    // moved to another channel: removed from the first
    assert(setLift(f, base, MotionChannel::H, 1.0).ok);
    assert(!f.overrideValue("SCROLL_V_MIX") && *f.overrideValue("SCROLL_H_MIX") == "x - rz + y");
    // a hand-written mix without y in another channel stays byte for byte
    f.setOverride("ZOOM_MIX", "ry   +   0 * rx");
    assert(setLift(f, base, MotionChannel::V, 2.0).ok);
    assert(*f.overrideValue("ZOOM_MIX") == "ry   +   0 * rx");
    // weight 0 removes it everywhere and the override goes away
    assert(setLift(f, base, MotionChannel::V, 0.0).ok);
    assert(!f.overrideValue("SCROLL_V_MIX") && !f.overrideValue("SCROLL_H_MIX"));
    assert(!setLift(f, base, MotionChannel::V, 1000.0).ok);
}

// Reading a form and assigning every motion its own action back changes nothing, for every built-in mode and
// every example mode file.
static void testRoundTrip() {
    const Settings base;
    const std::string root = tu::makeTempDir("dofscroll-motion-map");
    const std::filesystem::path examples = std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "modes";
    std::vector<std::string> warn;
    ModeSet sets[2] = {loadModes(root, &warn), loadModes(examples.string(), &warn)};
    int checked = 0;
    for (const ModeSet &ms : sets)
        for (const ModeDef &m : ms.modes) {
            const ModeForm orig = formFromMode(m);
            for (Motion mo : allMotions()) {
                if (mo == Motion::Lift) continue;
                ModeForm f = orig;
                assert(assign(f, base, mo, actionOf(f, base, mo)).ok);
                assert(f == orig);
            }
            ++checked;
        }
    assert(checked >= 10);
    tu::removeAll(root);
}

// Through the real editor: assign, save with the loader check, load again.
static void testFileResult() {
    const std::string root = tu::makeTempDir("dofscroll-motion-map-file");
    const std::string dir = root + "/modes";
    assert(fs::mkdirsPrivate(dir, nullptr));
    put(dir + "/demo.conf", "# keep me\nNAME = Demo\nV_ACTION = wheel_v\nUNKNOWN_LINE = stay\n");
    const ModeSet ms = loadModes(dir, nullptr);
    ModeEditor ed;
    std::string err;
    assert(ms.find("demo") && ed.open(dir + "/demo.conf", *ms.find("demo"), &err));
    const Settings base;
    assert(assign(ed.form(), base, Motion::VTilt, keys("pageup", "pagedown")).ok);
    assert(setReversed(ed.form(), base, Motion::VTilt, true).ok);
    assert(setLift(ed.form(), base, MotionChannel::Twist, 0.25).ok);
    const ModeEditor::SaveResult sr = ed.save();
    assert(sr.ok);
    const std::string text = get(dir + "/demo.conf");
    assert(has(text, "# keep me") && has(text, "UNKNOWN_LINE = stay"));
    assert(has(text, "V_PUSH_ACTION = wheel_v") && has(text, "V_TILT_ACTION = keys:pageup,pagedown"));
    assert(has(text, "SCROLL_V_TILT_SIGN = -1") && has(text, "ZOOM_MIX = ry + 0.25*y"));
    const ModeSet ms2 = loadModes(dir, nullptr);
    const ModeDef *m = ms2.find("demo");
    assert(m && m->vSplit.active() && m->vSplit.tilt.type == ChannelActionType::Keys);
    tu::removeAll(root);
}

int main() {
    testIds();
    testSplitAndCollapse();
    testCustomMix();
    testReverse();
    testLift();
    testRoundTrip();
    testFileResult();
    std::puts("test_ui_motion_map OK");
    return 0;
}
