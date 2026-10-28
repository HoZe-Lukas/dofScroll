// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Precedence of the profile channel actions (V_ACTION, H_ACTION, TWIST_ACTION, V_PUSH_ACTION, V_TILT_ACTION,
// H_PUSH_ACTION, H_TILT_ACTION) and of BUTTON_<n>: global < device < joystick mode < program (profile_actions.hpp,
// mode_runner.cpp). Pure functions and the real ModeRunner with a recording output; no threads.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "profile_actions.hpp"

using namespace spn;

struct PrecedenceRec : Output {
    std::vector<std::string> ev;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key k, bool d) override { ev.push_back(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { ev.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override { ev.push_back(std::string(d ? "+m" : "-m") + std::to_string(b)); }
    void releaseAll() override {}
    void flush() override {}
    int count(const std::string &e) const {
        int c = 0;
        for (const auto &x : ev) c += x == e;
        return c;
    }
};

static ChannelAction act(const char *text) {
    ChannelAction a;
    std::string err;
    const bool ok = parseChannelAction(text, &a, &err);
    assert(ok);
    (void)ok;
    return a;
}

static ModeDef mode(const char *id) {
    ModeDef m;
    m.id = m.label = id;
    return m;
}

struct Conf {
    ProfileSet ps;
    Conf &g(const char *k, const char *v) { ps.sections[0].kv.emplace_back(k, v); return *this; }
    Conf &dev(const char *pat, const char *k, const char *v) {
        ps.sections[ps.findOrAdd(Section::DEVICE, pat)].kv.emplace_back(k, v);
        return *this;
    }
    Conf &app(const char *pat, const char *k, const char *v) {
        ps.sections[ps.findOrAdd(Section::APP, pat)].kv.emplace_back(k, v);
        return *this;
    }
    Settings resolve(const char *program, const ModeDef *m = nullptr) const {
        return ps.resolve("SpaceMouse Pro", program, m ? &m->overrides : nullptr);
    }
};

using CAT = ChannelActionType;

static bool same(const ChannelActions &a, const ChannelActions &b) {
    return a.v == b.v && a.h == b.h && a.twist == b.twist && a.vSplit == b.vSplit && a.hSplit == b.hSplit;
}

// Every one of the seven keys: it takes effect from [global], from [device:*] and from [app:*], and only for a
// matching section.
static void testEachKeyEachLayer() {
    const char *keys[] = {"V_ACTION", "H_ACTION", "TWIST_ACTION", "V_PUSH_ACTION", "V_TILT_ACTION", "H_PUSH_ACTION", "H_TILT_ACTION"};
    const ChannelActions none = effectiveChannelActions(ModeDef(), Settings());
    assert(none.allDefault());
    for (const char *k : keys) {
        const Settings sg = Conf().g(k, "none").resolve("x");
        assert(sg.actions[0].any() && !sg.actions[1].any());
        assert(!effectiveChannelActions(ModeDef(), sg).allDefault());
        const Settings sd = Conf().dev("Pro", k, "none").resolve("x");
        assert(sd.actions[0].any() && !sd.actions[1].any());
        assert(!effectiveChannelActions(ModeDef(), sd).allDefault());
        const Settings sdo = Conf().dev("Other", k, "none").resolve("x");
        assert(!sdo.actions[0].any() && effectiveChannelActions(ModeDef(), sdo).allDefault());
        const Settings sa = Conf().app("x", k, "none").resolve("x");
        assert(!sa.actions[0].any() && sa.actions[1].any());
        assert(!effectiveChannelActions(ModeDef(), sa).allDefault());
        const Settings sn = Conf().app("x", k, "none").resolve("y");
        assert(!sn.actions[0].any() && !sn.actions[1].any() && effectiveChannelActions(ModeDef(), sn).allDefault());
        // app beats device beats global (the later layer decides): global wheel_v < device none < app wheel_h
        const Settings s3 = Conf().g(k, "wheel_v").dev("Pro", k, "none").app("x", k, "wheel_h").resolve("x");
        const Settings s2 = Conf().g(k, "wheel_v").dev("Pro", k, "none").resolve("x");
        const Settings s1 = Conf().g(k, "wheel_v").resolve("x");
        assert(!same(effectiveChannelActions(ModeDef(), s1), effectiveChannelActions(ModeDef(), s2)));
        assert(!same(effectiveChannelActions(ModeDef(), s2), effectiveChannelActions(ModeDef(), s3)));
        assert(!same(effectiveChannelActions(ModeDef(), s1), effectiveChannelActions(ModeDef(), s3)));
    }
}

static void testChannelValues() {
    {  // global < device: the device wins
        const Settings s = Conf().g("V_ACTION", "wheel_v").dev("Pro", "V_ACTION", "keys:up,down").resolve("x");
        const ChannelActions ca = effectiveChannelActions(ModeDef(), s);
        assert(ca.v.type == CAT::Keys && !ca.vSplit.active());
        assert(ca.h.type == CAT::Default && ca.twist.type == CAT::Default);
    }
    {  // device < mode < app for the twist
        ModeDef m = mode("m");
        m.twist = act("wheel_v");
        Conf c;
        c.g("TWIST_ACTION", "none").dev("Pro", "TWIST_ACTION", "zoom");
        assert(effectiveChannelActions(m, c.resolve("x", &m)).twist.type == CAT::WheelV);  // mode beats device
        c.app("x", "TWIST_ACTION", "keys:left,right");
        assert(effectiveChannelActions(m, c.resolve("x", &m)).twist.type == CAT::Keys);    // app beats mode
        assert(effectiveChannelActions(m, c.resolve("y", &m)).twist.type == CAT::WheelV);  // other program: mode
        ModeDef silent = mode("silent");                                                    // mode silent: device
        assert(effectiveChannelActions(silent, c.resolve("y", &silent)).twist.type == CAT::Zoom);
    }
    {  // H_ACTION: global none, mode silent -> none; mode sets -> mode; app -> app
        Conf c;
        c.g("H_ACTION", "none");
        ModeDef silent = mode("silent");
        assert(effectiveChannelActions(silent, c.resolve("x", &silent)).h.type == CAT::Off);
        ModeDef set = mode("set");
        set.h = act("keys:left,right");
        assert(effectiveChannelActions(set, c.resolve("x", &set)).h.type == CAT::Keys);
        c.app("x", "H_ACTION", "wheel_h");
        assert(effectiveChannelActions(set, c.resolve("x", &set)).h.type == CAT::WheelH);
    }
}

static void testSplitLayers() {
    {  // H_PUSH_ACTION of the device, H_TILT_ACTION of the program: both parts, channel untouched
        Conf c;
        c.dev("Pro", "H_PUSH_ACTION", "keys:left,right").app("x", "H_TILT_ACTION", "none");
        const ChannelActions ca = effectiveChannelActions(ModeDef(), c.resolve("x"));
        assert(ca.hSplit.pushSet && ca.hSplit.push.type == CAT::Keys);
        assert(ca.hSplit.tiltSet && ca.hSplit.tilt.type == CAT::Off);
        assert(ca.h.type == CAT::Default && !ca.vSplit.active());
    }
    {  // a split key of a higher layer replaces only its part
        Conf c;
        c.g("V_PUSH_ACTION", "none").g("V_TILT_ACTION", "none").app("x", "V_TILT_ACTION", "wheel_v");
        const ChannelActions ca = effectiveChannelActions(ModeDef(), c.resolve("x"));
        assert(ca.vSplit.push.type == CAT::Off && ca.vSplit.tilt.type == CAT::WheelV);
    }
    {  // the channel key of a layer drops the split of the layers below; a part above it comes back
        Conf c;
        // (a device V_ACTION over a [global] V_PUSH_ACTION is deliberately not asserted: both share layer 0 and the
        // split survives there, which differs from the header comment of profile_actions.hpp; see the PR)
        c.g("V_PUSH_ACTION", "none").app("x", "V_ACTION", "wheel_v");
        ChannelActions ca = effectiveChannelActions(ModeDef(), c.resolve("x"));
        assert(ca.v.type == CAT::WheelV && !ca.vSplit.active());
        c.app("x", "V_TILT_ACTION", "none");
        ca = effectiveChannelActions(ModeDef(), c.resolve("x"));
        assert(ca.v.type == CAT::WheelV && !ca.vSplit.pushSet && ca.vSplit.tiltSet);
    }
    {  // mode with a split part, [global] split below: the mode's part wins, the other part keeps the global
        ModeDef m = mode("m");
        m.vSplit.tilt = act("wheel_h");
        m.vSplit.tiltSet = true;
        Conf c;
        c.g("V_PUSH_ACTION", "none").g("V_TILT_ACTION", "none");
        const ChannelActions ca = effectiveChannelActions(m, c.resolve("x", &m));
        assert(ca.vSplit.push.type == CAT::Off && ca.vSplit.tilt.type == CAT::WheelH);
    }
    {  // the mode's own channel action drops the global split
        ModeDef m = mode("m");
        m.v = act("keys:up,down");
        Conf c;
        c.g("V_PUSH_ACTION", "none");
        const ChannelActions ca = effectiveChannelActions(m, c.resolve("x", &m));
        assert(ca.v.type == CAT::Keys && !ca.vSplit.active());
    }
    {  // the vertical keys never touch the horizontal channel and the other way round
        Conf c;
        c.g("V_ACTION", "none").app("x", "H_PUSH_ACTION", "none");
        const ChannelActions ca = effectiveChannelActions(ModeDef(), c.resolve("x"));
        assert(ca.v.type == CAT::Off && !ca.vSplit.active() && ca.h.type == CAT::Default && ca.hSplit.pushSet && !ca.hSplit.tiltSet);
    }
}

static void tickButtons(ModeRunner &r, const Settings &s, uint32_t btn) {
    RawAxes raw{};
    for (int i = 0; i < 20; ++i) r.tick(s, raw, btn, true, 1.0 / 250.0);
}

static void testButtons() {
    ModeDef m = mode("b");
    ButtonBinding b1;
    b1.button = 1;
    b1.type = ButtonActionType::Key;
    b1.key = Key::Space;
    m.buttons = {b1};
    {  // device beats global (no mode binding)
        Conf c;
        c.g("BUTTON_2", "rightclick").dev("Pro", "BUTTON_2", "middleclick");
        PrecedenceRec o;
        ModeRunner r(&o);
        tickButtons(r, c.resolve("x"), 2u);
        assert(o.count("+m2") == 1 && o.count("+m3") == 0);
    }
    {  // the device does not beat the mode, the program does
        Conf c;
        c.dev("Pro", "BUTTON_1", "click");
        PrecedenceRec o;
        ModeRunner r(&o);
        r.setMode(m);
        tickButtons(r, c.resolve("x", &m), 1u);
        assert(o.count("+space") == 1 && o.count("+m1") == 0);
        c.app("x", "BUTTON_1", "rightclick");
        PrecedenceRec o2;
        ModeRunner r2(&o2);
        r2.setMode(m);
        tickButtons(r2, c.resolve("x", &m), 1u);
        assert(o2.count("+space") == 0 && o2.count("+m3") == 1 && o2.count("+m1") == 0);
        PrecedenceRec o3;  // another program: the mode again
        ModeRunner r3(&o3);
        r3.setMode(m);
        tickButtons(r3, c.resolve("y", &m), 1u);
        assert(o3.count("+space") == 1 && o3.count("+m3") == 0);
    }
    {  // a program binding on a button the mode does not use, and "none" on one the mode binds
        Conf c;
        c.app("x", "BUTTON_3", "middleclick").app("x", "BUTTON_1", "none");
        PrecedenceRec o;
        ModeRunner r(&o);
        r.setMode(m);
        tickButtons(r, c.resolve("x", &m), 5u);  // buttons 1 and 3
        assert(o.count("+space") == 0 && o.count("+m2") == 1);
    }
    {  // edge bindings: a program's mode:<id> beats the mode's, a program "none" removes the mode's switch
        ModeDef sw = mode("sw");
        ButtonBinding s1;
        s1.button = 1;
        s1.type = ButtonActionType::ModeSwitch;
        s1.mode = "next";
        sw.buttons = {s1};
        Conf c;
        c.app("x", "BUTTON_1", "mode:other").app("z", "BUTTON_1", "none");
        PrecedenceRec o;
        ModeRunner r(&o);
        r.setMode(sw);
        tickButtons(r, c.resolve("x", &sw), 1u);
        assert(r.takeSwitchRequest() == "other");
        PrecedenceRec o2;
        ModeRunner r2(&o2);
        r2.setMode(sw);
        tickButtons(r2, c.resolve("z", &sw), 1u);
        assert(r2.takeSwitchRequest().empty());
        PrecedenceRec o3;
        ModeRunner r3(&o3);
        r3.setMode(sw);
        tickButtons(r3, c.resolve("y", &sw), 1u);
        assert(r3.takeSwitchRequest() == "next");
    }
    {  // app_buttons bookkeeping
        Conf c;
        c.app("x", "BUTTON_4", "click");
        assert(c.resolve("x").app_buttons.count(4) && c.resolve("y").app_buttons.empty());
        assert(c.resolve("y").buttons.count(4) == 0);
    }
}

int main() {
    testEachKeyEachLayer();
    testChannelValues();
    testSplitLayers();
    testButtons();
    std::puts("test_profile_precedence: OK");
    return 0;
}
