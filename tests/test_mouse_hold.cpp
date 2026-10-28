// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Mouse and hold (stage 4 of #224, docs/plan-motion-functions.md): `click`, `rightclick`, `middleclick` per side of
// step:<neg>,<pos> (one click per trigger crossing) and hold:<neg>,<pos> (a key, chord or mouse button is held while the motion is
// deflected beyond the trigger and let go at half of it) on every motion, plus the audit findings of the lift slot in
// ModeRunner::tickCustom. The release paths of a daemon (reload, ENABLED = 0, ...) are in tests/test_stuck_keys.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "config.hpp"
#include "key_engine.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// Keeps the state of the output (keys and mouse buttons held) and counts the clicks (a button going down).
struct MouseRec : Output {
    std::set<int> keys, mouse;
    std::vector<int> clicks;  // the button of each press, in order
    int taps = 0, tapOf[kKeyCount] = {};
    int presses[4] = {0, 0, 0, 0};
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key k, bool d) override {
        if (d) keys.insert(int(k)); else keys.erase(int(k));
    }
    void tapKey(Key k) override { ++taps; ++tapOf[int(k)]; }
    void mouseButton(int b, bool d) override {
        if (d) {
            mouse.insert(b);
            clicks.push_back(b);
            if (b >= 0 && b < 4) ++presses[b];
        } else {
            mouse.erase(b);
        }
    }
    void releaseAll() override { keys.clear(); mouse.clear(); }
    void flush() override {}
    bool held() const { return !keys.empty() || !mouse.empty(); }
    bool key(Key k) const { return keys.count(int(k)) != 0; }
};

static const double DT = 1.0 / 250.0;

static Settings conf(const std::vector<KV> &kv) {
    Settings s;
    for (const KV &p : kv) {
        std::string err;
        const bool ok = applyKeyLayer(s, p.first, p.second, kLayerGlobal, &err);
        if (!ok) std::fprintf(stderr, "bad key %s = %s: %s\n", p.first.c_str(), p.second.c_str(), err.c_str());
        assert(ok);
    }
    return s;
}

static RawAxes axis(Axis a, int v) {
    RawAxes r{};
    r[a] = v;
    return r;
}

static ModeDef normalMode() {
    ModeDef m;
    m.id = "normal";
    return m;
}

static void run(ModeRunner &r, const Settings &s, const RawAxes &raw, double secs) {
    for (int i = 0; i < static_cast<int>(secs / DT + 0.5); ++i) r.tick(s, raw, 0, true, DT);
}

// ---------------------------------------------------------------- parser
static void testParser() {
    ChannelAction a;
    std::string err;
    CHECK(parseChannelAction("step:click,rightclick", &a, &err) && a.type == ChannelActionType::Step && a.negMouse == kMouseLeft &&
              a.posMouse == kMouseRight && !a.negNone && !a.posNone,
          "step with two clicks: %s", err.c_str());
    CHECK(channelActionText(a) == "step:click,rightclick", "round trip: %s", channelActionText(a).c_str());
    CHECK(parseChannelAction("step:none,middleclick", &a, &err) && a.negNone && a.posMouse == kMouseMiddle, "none per side with a click");
    CHECK(channelActionText(a) == "step:none,middleclick", "round trip of none + middleclick");
    CHECK(parseChannelAction("step:ctrl+left,click", &a, &err) && a.negMods.n == 1 && a.posMouse == kMouseLeft, "a chord and a click");
    CHECK(parseChannelAction("hold:click,none", &a, &err) && a.type == ChannelActionType::Hold && a.negMouse == kMouseLeft && a.posNone, "hold a mouse button");
    CHECK(channelActionText(a) == "hold:click,none", "hold round trip");
    CHECK(parseChannelAction("hold:ctrl+shift,alt", &a, &err) && a.type == ChannelActionType::Hold, "hold a chord and a lone modifier: %s", err.c_str());
    CHECK(channelActionText(a) == "hold:ctrl+shift,alt", "hold chord round trip: %s", channelActionText(a).c_str());
    CHECK(parseChannelAction(" HOLD: Click , RightClick ", &a, &err) && a.negMouse == kMouseLeft && a.posMouse == kMouseRight, "case and blanks");
    // refused, with a message that names the problem
    CHECK(!parseChannelAction("keys:click,none", &a, &err) && err.find("digitally") != std::string::npos, "analog click refused: %s", err.c_str());
    CHECK(!parseChannelAction("keys:up,rightclick", &a, &err), "analog right click refused");
    CHECK(!parseChannelAction("keys:a,down", &a, &err) && err.find("digitally") != std::string::npos, "analog risky key stays refused");
    CHECK(!parseChannelAction("hold:none,none", &a, &err) && err.find("at least one side") != std::string::npos, "hold needs a side: %s", err.c_str());
    CHECK(!parseChannelAction("hold:click", &a, &err) && err.find("two sides") != std::string::npos, "hold needs two sides: %s", err.c_str());
    CHECK(!parseChannelAction("hold:click,none,none", &a, &err), "three sides refused");
    CHECK(!parseChannelAction("hold:ctrl+,none", &a, &err), "a hold chord must end with a key");
    CHECK(!parseChannelAction("hold:nonsense,none", &a, &err) && err.find("unknown key") != std::string::npos, "unknown key in a hold: %s", err.c_str());
    CHECK(!parseChannelAction("step:click,doubleclick", &a, &err), "unknown mouse word refused");
    // equality sees the mouse side
    ChannelAction b, c;
    parseChannelAction("step:click,none", &b, nullptr);
    parseChannelAction("step:rightclick,none", &c, nullptr);
    CHECK(!(b == c), "click and rightclick differ");
    // lift: the digital forms are allowed, the analog one is not
    CHECK(parseLiftAction("hold:click,none", &a, &err) && parseLiftAction("step:none,click", &a, &err), "lift takes click and hold");
    CHECK(!parseLiftAction("keys:up,down", &a, &err), "lift refuses analog keys");
    // the button grammar is unchanged
    ButtonActionType t;
    Key k;
    CHECK(parseButtonAction("click", &t, &k, &err) && t == ButtonActionType::Click, "button click unchanged");
    CHECK(parseButtonAction("rightclick", &t, &k, &err) && t == ButtonActionType::RightClick, "button rightclick unchanged");
    // profile keys reach the parser through applyKey
    Settings s;
    CHECK(applyKey(s, "V_TILT_ACTION", "hold:click,none", &err), "V_TILT_ACTION accepts a hold: %s", err.c_str());
    CHECK(!applyKey(s, "V_TILT_ACTION", "keys:click,none", &err), "V_TILT_ACTION refuses an analog click");
    CHECK(applyKey(s, "LIFT_ACTION", "step:none,click", &err), "LIFT_ACTION accepts a click: %s", err.c_str());
}

static void testRiskWarnings() {
    // The modes loader loads mouse clicks and holds (tier B: the plain warning, no gate) and says so.
    const std::string dir = tu::makeTempDir("dofscroll-mouse-hold-test");
    {
        std::ofstream f(dir + "/clicker.conf");
        f << "V_ACTION = step:click,none\nH_ACTION = hold:ctrl,none\nTWIST_ACTION = keys:click,none\n";
    }
    std::vector<std::string> warnings;
    const ModeSet ms = loadModes(dir, &warnings);
    const ModeDef *m = ms.find("clicker");
    CHECK(m && m->v.type == ChannelActionType::Step && m->h.type == ChannelActionType::Hold, "the mode loads with a click and a hold");
    CHECK(m && m->twist.type == ChannelActionType::Default, "the analog click line is ignored");
    std::string all;
    for (const std::string &w : warnings) all += w + "\n";
    CHECK(all.find("clicks the mouse") != std::string::npos && all.find("holds") != std::string::npos, "warnings: %s", all.c_str());
    CHECK(all.find("digitally") != std::string::npos, "the refused analog click says why: %s", all.c_str());
    tu::removeAll(dir);
}

// ---------------------------------------------------------------- scenarios
struct Motion {
    const char *name;
    const char *key;  // the action key
    Axis axis;
    int raw;
    const char *extra;  // settings needed to reach the motion (dead zone of the lift)
};
static const Motion kMotions[] = {
    {"v_push", "V_PUSH_ACTION", AX_Z, 300, nullptr},
    {"v_tilt", "V_TILT_ACTION", AX_RX, 300, nullptr},
    {"h_push", "H_PUSH_ACTION", AX_X, 300, nullptr},
    {"h_tilt", "H_TILT_ACTION", AX_RZ, 300, nullptr},
    {"twist", "TWIST_ACTION", AX_RY, 300, nullptr},
    {"lift", "LIFT_ACTION", AX_Y, 300, "LIFT_DEADZONE"},
};

static std::vector<KV> motionConf(const Motion &m, const std::string &action) {
    std::vector<KV> kv = {{m.key, action}};
    if (m.extra) kv.push_back({m.extra, "20"});
    return kv;
}

// One click per crossing, on both sides, on every motion; the other side is another button.
static void testClickEveryMotion() {
    for (const Motion &m : kMotions) {
        const Settings s = conf(motionConf(m, "step:click,rightclick"));
        for (int dir = -1; dir <= 1; dir += 2) {
            MouseRec o;
            ModeRunner r(&o);
            r.setMode(normalMode());
            run(r, s, axis(m.axis, dir * m.raw), 1.0);
            CHECK(o.clicks.size() == 1, "%s dir %d: one click for a long push, got %zu", m.name, dir, o.clicks.size());
            CHECK(!o.held(), "%s dir %d: a click is not held", m.name, dir);
            // exactly one of the two sides fired, and a different one for the other direction
            static int firstDir = 0;
            if (dir == -1) firstDir = o.clicks.empty() ? 0 : o.clicks[0];
            else CHECK(!o.clicks.empty() && o.clicks[0] != firstDir, "%s: the two directions click different buttons", m.name);
        }
    }
}

static void testClickKinds() {
    const Motion &m = kMotions[0];
    struct { const char *word; int button; } kinds[] = {{"click", 1}, {"middleclick", 2}, {"rightclick", 3}};
    for (const auto &k : kinds) {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        // the side that fires depends on the sign of the motion; fill both with the same button
        run(r, conf(motionConf(m, std::string("step:") + k.word + "," + k.word)), axis(m.axis, 300), 0.5);
        CHECK(o.clicks.size() == 1 && o.clicks[0] == k.button, "%s is mouse button %d", k.word, k.button);
    }
}

// Pushes again after easing back: one more click per crossing. Easing back only to above half of the trigger does not re-arm.
static void testOneTapPerCrossing() {
    const Motion &m = kMotions[0];
    const Settings s = conf(motionConf(m, "step:click,click"));
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    for (int i = 0; i < 3; ++i) {
        run(r, s, axis(m.axis, 300), 0.4);
        run(r, s, axis(m.axis, 0), 0.4);
    }
    CHECK(o.clicks.size() == 3, "three crossings, three clicks, got %zu", o.clicks.size());
    // a wobble that stays above half of the trigger fires nothing more
    MouseRec o2;
    ModeRunner r2(&o2);
    r2.setMode(normalMode());
    for (int i = 0; i < 6; ++i) run(r2, s, axis(m.axis, i % 2 ? 300 : 200), 0.2);
    CHECK(o2.clicks.size() == 1, "a wobble above half the level fires once, got %zu", o2.clicks.size());
}

static void testNoneSide() {
    for (const Motion &m : kMotions) {
        const Settings s = conf(motionConf(m, "step:none,click"));
        int fired = 0;
        for (int dir = -1; dir <= 1; dir += 2) {
            MouseRec o;
            ModeRunner r(&o);
            r.setMode(normalMode());
            run(r, s, axis(m.axis, dir * m.raw), 0.5);
            fired += int(o.clicks.size());
        }
        CHECK(fired == 1, "%s: none for one side, a click for the other (%d clicks in total)", m.name, fired);
    }
}

// Push and tilt of the same channel at once: a split channel runs two triggers, an unsplit one a single trigger. The crosstalk
// of a push (a little tilt, below the trigger) fires nothing on the tilt part, and a channel with one action fires only once.
static void testCrosstalk() {
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_PUSH_ACTION", "step:click,click"}, {"V_TILT_ACTION", "step:rightclick,rightclick"}});
        RawAxes raw = axis(AX_Z, 300);
        raw[AX_RX] = 25;
        run(r, s, raw, 1.0);
        CHECK(o.presses[1] == 1 && o.presses[3] == 0, "push fires its click, the small tilt nothing (%d / %d)", o.presses[1], o.presses[3]);
    }
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_ACTION", "step:click,click"}});
        RawAxes raw = axis(AX_Z, 300);
        raw[AX_RX] = 300;
        run(r, s, raw, 1.0);
        CHECK(o.clicks.size() == 1, "one channel, push and tilt together: one click, got %zu", o.clicks.size());
    }
}

// The raw value of z whose rate is the first at or above `target` (the same for the trigger tests below).
static int rawForRate(double target) {
    for (int raw = 1; raw <= 350; ++raw) {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_PUSH_ACTION", "hold:click,click"}});
        run(r, s, axis(AX_Z, raw), 0.5);
        if (std::fabs(r.info().rate_part[PART_V_PUSH]) >= target) return raw;
    }
    return 350;
}

// hold: held while deflected beyond the trigger, let go at half of it, not before.
static void testHoldHysteresis() {
    const double T = 20.0;  // STEP_TRIGGER default
    const Settings s = conf({{"V_PUSH_ACTION", "hold:click,click"}});
    const int hi = rawForRate(T * 1.1), mid = rawForRate(T * 0.7), low = rawForRate(T * 0.3);
    CHECK(low < mid && mid < hi, "raw levels %d < %d < %d", low, mid, hi);
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    run(r, s, axis(AX_Z, low), 0.5);
    CHECK(!o.held(), "below the trigger nothing is held");
    run(r, s, axis(AX_Z, mid), 0.5);
    CHECK(!o.held(), "between half and the trigger, from rest, nothing is held yet");
    run(r, s, axis(AX_Z, hi), 0.5);
    CHECK(o.mouse.size() == 1 && o.clicks.size() == 1, "beyond the trigger the button goes down once");
    run(r, s, axis(AX_Z, mid), 0.5);
    CHECK(o.mouse.size() == 1 && o.clicks.size() == 1, "easing back to above half keeps it held");
    run(r, s, axis(AX_Z, low), 0.5);
    CHECK(!o.held(), "below half of the trigger the button is let go");
    run(r, s, axis(AX_Z, hi), 0.5);
    CHECK(o.mouse.size() == 1 && o.clicks.size() == 2, "pushing again holds again");
    run(r, s, axis(AX_Z, 0), 0.5);
    CHECK(!o.held(), "at rest nothing is held");
}

// hold on every motion: a mouse button on one side, a key on the other; both sides release; every motion.
static void testHoldEveryMotion() {
    for (const Motion &m : kMotions) {
        const Settings s = conf(motionConf(m, "hold:click,ctrl+alt"));
        bool sawMouse = false, sawChord = false;
        for (int dir = -1; dir <= 1; dir += 2) {
            MouseRec o;
            ModeRunner r(&o);
            r.setMode(normalMode());
            run(r, s, axis(m.axis, dir * m.raw), 0.5);
            if (!o.mouse.empty()) sawMouse = true;
            if (o.key(Key::Ctrl) && o.key(Key::Alt)) sawChord = true;
            CHECK(o.mouse.size() + o.keys.size() >= 1, "%s dir %d: something is held while deflected", m.name, dir);
            run(r, s, axis(m.axis, 0), 0.5);
            CHECK(!o.held(), "%s dir %d: released at rest (%zu keys, %zu mouse)", m.name, dir, o.keys.size(), o.mouse.size());
            CHECK(o.taps == 0, "%s: a hold never taps", m.name);
        }
        CHECK(sawMouse && sawChord, "%s: one direction holds the button, the other the chord", m.name);
    }
}

// Per side: the two sides are armed and held on their own.
static void testHoldPerSide() {
    const Motion &m = kMotions[0];
    const Settings s = conf(motionConf(m, "hold:ctrl,alt"));
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    run(r, s, axis(m.axis, 300), 0.4);
    const bool first = o.key(Key::Ctrl);
    CHECK(first != o.key(Key::Alt), "one side is held");
    run(r, s, axis(m.axis, 0), 0.4);
    CHECK(!o.held(), "released at rest");
    run(r, s, axis(m.axis, -300), 0.4);
    CHECK(o.key(first ? Key::Alt : Key::Ctrl) && !o.key(first ? Key::Ctrl : Key::Alt), "the other direction holds the other key");
    run(r, s, axis(m.axis, 0), 0.4);
    CHECK(!o.held(), "released again");
}

static void testHoldNoneSide() {
    const Motion &m = kMotions[1];
    const Settings s = conf(motionConf(m, "hold:none,click"));
    int held = 0;
    for (int dir = -1; dir <= 1; dir += 2) {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        run(r, s, axis(m.axis, dir * m.raw), 0.5);
        held += int(o.mouse.size());
    }
    CHECK(held == 1, "none for one side: only the other direction holds (%d)", held);
}

// The twist zooms (it holds the zoom modifier, ctrl); a hold of ctrl on a tilt keeps ctrl down until BOTH let go.
static void testZoomPlusHoldUnion() {
    const Settings s = conf({{"V_TILT_ACTION", "hold:ctrl,ctrl"}, {"TWIST_ACTION", "zoom"}});
    {  // the hold ends first: the zoom still needs ctrl
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        RawAxes both = axis(AX_RY, 300);
        run(r, s, both, 0.5);
        CHECK(r.info().zooming && o.key(Key::Ctrl), "the twist zooms and holds ctrl");
        both[AX_RX] = 300;
        run(r, s, both, 0.4);
        CHECK(o.key(Key::Ctrl), "ctrl is down with zoom and hold");
        run(r, s, axis(AX_RY, 300), 0.4);  // the tilt lets go
        CHECK(r.info().zooming && o.key(Key::Ctrl), "the hold ended, the zoom still holds ctrl");
        run(r, s, axis(AX_RY, 0), 1.0);
        CHECK(!o.key(Key::Ctrl), "ctrl is up when both are done");
    }
    {  // the zoom ends first: the hold keeps ctrl
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        RawAxes both = axis(AX_RY, 300);
        both[AX_RX] = 300;
        run(r, s, both, 0.5);
        run(r, s, axis(AX_RX, 300), 1.0);  // the twist lets go (the latch ends after its delay)
        CHECK(!r.info().zooming, "the zoom ended");
        CHECK(o.key(Key::Ctrl), "the hold keeps ctrl down after the zoom ended");
        run(r, s, axis(AX_RX, 0), 0.5);
        CHECK(!o.key(Key::Ctrl), "ctrl is up when the hold ends too");
    }
}

// Two holds of the same modifier: the union, up only when both let go.
static void testTwoHoldsUnion() {
    const Settings s = conf({{"V_PUSH_ACTION", "hold:ctrl+shift,ctrl+shift"}, {"H_PUSH_ACTION", "hold:ctrl+alt,ctrl+alt"}});
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    RawAxes both = axis(AX_Z, 300);
    both[AX_X] = 300;
    run(r, s, both, 0.5);
    CHECK(o.key(Key::Ctrl) && o.key(Key::Shift) && o.key(Key::Alt), "the union of both chords is held");
    run(r, s, axis(AX_X, 300), 0.5);  // the first lets go
    CHECK(o.key(Key::Ctrl) && o.key(Key::Alt) && !o.key(Key::Shift), "ctrl stays for the second hold, shift goes");
    run(r, s, axis(AX_X, 0), 0.5);
    CHECK(!o.held(), "nothing held when both are done");
}

// A hold longer than 60 s is not released by the runner (it is only logged once).
static void testLongHold() {
    const Settings s = conf({{"V_PUSH_ACTION", "hold:click,click"}});
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    run(r, s, axis(AX_Z, 300), 65.0);
    CHECK(o.mouse.size() == 1 && o.clicks.size() == 1, "still held after 65 s, pressed once");
    run(r, s, axis(AX_Z, 0), 0.3);
    CHECK(!o.held(), "released when the motion falls back");
}

// A mouse button held by a hold is not clicked by a step of another motion (its release would end the drag).
static void testClickSkipsHeldButton() {
    const Settings s = conf({{"V_PUSH_ACTION", "hold:click,click"}, {"H_PUSH_ACTION", "step:click,click"}});
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    RawAxes both = axis(AX_Z, 300);
    both[AX_X] = 300;
    run(r, s, both, 0.5);
    CHECK(o.mouse.size() == 1 && o.clicks.size() == 1, "the drag is not interrupted by a click of the same button");
}

// The release paths of the runner itself: releaseAll(), a mode switch and a settings change with every action back to default.
static void testRunnerRelease() {
    const Settings s = conf({{"V_PUSH_ACTION", "hold:click,click"}, {"H_PUSH_ACTION", "hold:ctrl,ctrl"}});
    RawAxes both = axis(AX_Z, 300);
    both[AX_X] = 300;
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        run(r, s, both, 0.5);
        CHECK(o.held(), "held");
        r.releaseAll();
        CHECK(!o.held() && !r.holdsAnything(), "releaseAll lets go of holds");
        r.tick(s, axis(AX_Z, 0), 0, true, DT);
    }
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        run(r, s, both, 0.5);
        r.setMode(normalMode());
        CHECK(!o.held(), "a mode switch lets go of holds");
    }
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        run(r, s, both, 0.5);
        run(r, conf({}), both, 0.1);  // reload: every action is default again, the cap is still deflected
        CHECK(!o.held(), "the legacy path lets go of holds");
    }
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        run(r, s, both, 0.5);
        run(r, s, RawAxes{}, 0.1);  // device removed or stale: the caller feeds zero
        CHECK(!o.held(), "a zero source lets go of holds");
    }
}

// ---------------------------------------------------------------- audit: lift state outside the custom path
static int ticksToFirstTap(ModeRunner &r, MouseRec &o, const Settings &s, const RawAxes &raw, int maxTicks) {
    const int before = o.taps;
    for (int i = 0; i < maxTicks; ++i) {
        r.tick(s, raw, 0, true, DT);
        if (o.taps > before) return i;
    }
    return -1;
}

static void testLiftStateResetOutsideCustomPath() {
    const std::vector<KV> lift = {{"LIFT_ACTION", "step:volume_up,volume_down"}, {"LIFT_DEADZONE", "20"}, {"LIFT_SMOOTH_MS", "400"}};
    const Settings custom = conf(lift);
    const Settings plain = conf({});
    const RawAxes up = axis(AX_Y, 120);
    // reference: a fresh runner, how long the smoothed lift takes to fire
    int fresh = -1;
    {
        MouseRec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        fresh = ticksToFirstTap(r, o, custom, up, 2000);
    }
    CHECK(fresh > 0, "the smoothed lift fires after a delay (%d ticks)", fresh);
    // charge the filter part of the way, then every action is default for a while (a reload), then the action is back
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    for (int i = 0; i < fresh / 2; ++i) r.tick(custom, up, 0, true, DT);
    CHECK(o.taps == 0, "not yet fired");
    for (int i = 0; i < 10; ++i) r.tick(plain, up, 0, true, DT);  // legacy path
    const int again = ticksToFirstTap(r, o, custom, up, 2000);
    CHECK(again == fresh, "after leaving the custom path the lift starts from rest again (%d ticks, fresh %d)", again, fresh);
}

static void testLiftStepSafeAtZoomEnd() {
    const Settings s = conf({{"LIFT_ACTION", "step:volume_up,volume_down"}, {"LIFT_DEADZONE", "20"}});
    MouseRec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    RawAxes zoomAndLift = axis(AX_RY, 350);
    zoomAndLift[AX_Y] = 150;
    run(r, s, zoomAndLift, 1.0);
    const bool zoomed = r.info().zooming;
    CHECK(zoomed, "the twist zooms with the cap lifted");
    const int taps = o.taps;
    // the twist lets go while the cap stays lifted beyond the trigger: the step must not fire when the zoom ends
    run(r, s, axis(AX_Y, 150), 2.0);
    CHECK(!r.info().zooming, "the zoom ended");
    CHECK(o.taps == taps, "no step fires the moment the zoom ends (%d extra taps)", o.taps - taps);
    // lifting again after rest does press
    run(r, s, RawAxes{}, 0.5);
    run(r, s, axis(AX_Y, 150), 0.5);
    CHECK(o.taps == taps + 1, "a new lift presses once (%d)", o.taps - taps);
}

int main() {
    testParser();
    testRiskWarnings();
    testClickEveryMotion();
    testClickKinds();
    testOneTapPerCrossing();
    testNoneSide();
    testCrosstalk();
    testHoldHysteresis();
    testHoldEveryMotion();
    testHoldPerSide();
    testHoldNoneSide();
    testZoomPlusHoldUnion();
    testTwoHoldsUnion();
    testLongHold();
    testClickSkipsHeldButton();
    testRunnerRelease();
    testLiftStateResetOutsideCustomPath();
    testLiftStepSafeAtZoomEnd();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_mouse_hold: all passed");
    return 0;
}
