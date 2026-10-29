// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The macro engine of the ModeRunner (stage 7b of docs/plan-motion-functions.md) with a fake output: timing of the pauses (dt), the
// release of everything a macro holds at its end and on every abort path, a trigger while one runs is ignored, the 256 step
// queue, the press-only macro, the release part, and macros on a button and on every motion. The daemon-level paths
// (reload, suspend, ENABLED = 0, device removal, the safety net) are in tests/test_stuck_keys.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "config.hpp"
#include "macro.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// Records every key event with the tick it happened in: "+name" for a key going down, "-name" for it going up, "tap:name".
struct MacroRec : Output {
    const int *tick = nullptr;
    std::vector<std::string> log;
    std::vector<int> at;  // the tick of each log entry
    std::set<int> keys, mouse;
    int releaseAlls = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key k, bool d) override {
        if (d) keys.insert(int(k)); else keys.erase(int(k));
        add(std::string(d ? "+" : "-") + keyName(k));
    }
    void tapKey(Key k) override { add(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override {
        if (d) mouse.insert(b); else mouse.erase(b);
    }
    void releaseAll() override { keys.clear(); mouse.clear(); ++releaseAlls; }
    void flush() override {}
    void add(const std::string &e) {
        log.push_back(e);
        at.push_back(tick ? *tick : 0);
    }
    int count(const std::string &e) const {
        int n = 0;
        for (const std::string &x : log) n += x == e;
        return n;
    }
    // the tick of the n-th (0-based) entry e, -1 if there is none
    int tickOf(const std::string &e, int nth = 0) const {
        for (size_t i = 0; i < log.size(); ++i)
            if (log[i] == e && nth-- == 0) return at[i];
        return -1;
    }
    std::string text() const {
        std::string t;
        for (const std::string &x : log) t += (t.empty() ? "" : " ") + x;
        return t;
    }
};

// A runner with a tick counter: one tick is 1/250 s (4 ms).
struct MacroRig {
    MacroRec out;
    ModeRunner runner;
    Settings s;
    int tick = 0;
    uint32_t buttons = 0;
    RawAxes raw{};
    static constexpr double kDt = 1.0 / 250.0;
    MacroRig() : runner(&out) {
        out.tick = &tick;
        ModeDef m;
        m.id = "normal";
        runner.setMode(m);
    }
    void apply(const std::string &key, const std::string &value) {
        std::string err;
        const bool ok = applyKeyLayer(s, key, value, kLayerGlobal, &err);
        if (!ok) std::fprintf(stderr, "bad %s = %s: %s\n", key.c_str(), value.c_str(), err.c_str());
        assert(ok);
    }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) {
            runner.tick(s, raw, buttons, true, kDt);
            ++tick;
        }
    }
    void press(int button) { buttons |= 1u << (button - 1); }
    void release(int button) { buttons &= ~(1u << (button - 1)); }
    // press and release a button, one tick each
    void tap(int button) {
        press(button);
        step();
        release(button);
        step();
    }
    void axis(Axis a, int v) {
        raw = RawAxes{};
        raw[a] = v;
    }
};

static std::string repeat(const std::string &step, size_t n) {
    std::string t;
    for (size_t i = 0; i < n; ++i) t += (i ? " " : "") + step;
    return t;
}

// ---------------------------------------------------------------- button macro: order, timing, release at the end
static void testButtonOrderAndTiming() {
    MacroRig g;
    g.apply("BUTTON_5", "macro:+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b");
    g.step(2);
    CHECK(g.out.log.empty(), "nothing before the button: %s", g.out.text().c_str());
    g.tap(5);
    // the first steps run in the tick of the press, in order
    CHECK(g.out.log.size() >= 2 && g.out.log[0] == "+ctrl" && g.out.log[1] == "+a" && g.out.at[0] == 2 && g.out.at[1] == 2, "first steps at once: %s", g.out.text().c_str());
    CHECK(g.out.keys.count(int(Key::A)) == 1, "a is held during the pause");
    g.step(60);
    CHECK(g.out.text() == "+ctrl +a -a +c -c -ctrl +shift +a -a +b -b -shift", "all steps ran in order, shift released at the end: %s", g.out.text().c_str());
    CHECK(g.out.keys.empty() && !g.runner.macroRunning() && !g.runner.holdsAnything(), "nothing held after the macro");
    // 4 ms ticks: a 30 ms pause ends in the 8th tick after the step (32 ms >= 30 ms), the carry (2 ms) shortens the next pause
    const int t0 = g.out.tickOf("+a");
    const int t1 = g.out.tickOf("-a");
    CHECK(t1 - t0 == 8, "30 ms is 8 ticks of 4 ms: %d", t1 - t0);
    CHECK(g.out.tickOf("+c") == t1, "the step after a pause runs in the tick the pause ends");
    CHECK(g.out.tickOf("-c") - g.out.tickOf("+c") == 7, "the 2 ms carried over shorten the next pause to 7 ticks: %d", g.out.tickOf("-c") - g.out.tickOf("+c"));
    CHECK(g.out.tickOf("-shift") == g.out.tickOf("-b"), "the end release comes in the tick of the last step");
}

static void testExactPauseTiming() {
    // 8 ms is exactly two ticks of 4 ms
    MacroRig g;
    g.apply("BUTTON_1", "macro:+a wait:8 -a");
    g.tap(1);
    g.step(5);
    CHECK(g.out.tickOf("-a") - g.out.tickOf("+a") == 2, "8 ms is two ticks: %d", g.out.tickOf("-a") - g.out.tickOf("+a"));
    // a pause shorter than a tick ends in the next tick
    MacroRig h;
    h.apply("BUTTON_1", "macro:+a wait:1 -a");
    h.tap(1);
    h.step(3);
    CHECK(h.out.tickOf("-a") - h.out.tickOf("+a") == 1, "1 ms ends in the next tick");
    // the longest pause: 60 s are 15000 ticks
    MacroRig k;
    k.apply("BUTTON_1", "macro:+a wait:60000 -a");
    k.tap(1);
    k.step(14990);
    CHECK(k.runner.macroRunning() && k.out.count("-a") == 0, "still waiting after 59.96 s");
    k.step(20);
    CHECK(!k.runner.macroRunning() && k.out.count("-a") == 1, "done after 60 s");
}

static void testTrailingPause() {
    // a pause at the end counts: the macro ends after it, and what it holds is released then
    MacroRig g;
    g.apply("BUTTON_1", "macro:+ctrl wait:100");
    g.tap(1);
    g.step(20);
    CHECK(g.out.keys.count(int(Key::Ctrl)) == 1 && g.runner.macroRunning(), "ctrl is held during the trailing pause");
    g.step(10);
    CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "released after it: %s", g.out.text().c_str());
    const int held = g.out.tickOf("-ctrl") - g.out.tickOf("+ctrl");
    CHECK(held >= 25 && held <= 26, "held for 100 ms (25 ticks): %d", held);
}

static void testPressOnlyMacro() {
    MacroRig g;
    g.apply("BUTTON_2", "macro:+ctrl");
    g.tap(2);
    CHECK(g.out.text() == "+ctrl -ctrl", "a press-only macro presses and lets go at its end: %s", g.out.text().c_str());
    CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "nothing held");
    MacroRig h;
    h.apply("BUTTON_2", "macro:+ctrl+shift+a wait:20");
    h.tap(2);
    h.step(10);
    CHECK(h.out.count("+ctrl") == 1 && h.out.count("+shift") == 1 && h.out.count("+a") == 1, "a chord was pressed: %s", h.out.text().c_str());
    CHECK(h.out.keys.empty() && h.out.count("-ctrl") == 1 && h.out.count("-shift") == 1 && h.out.count("-a") == 1, "and all of it released at the end: %s", h.out.text().c_str());
}

static void testChordOrder() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:+ctrl+shift+a -ctrl+shift+a");
    g.tap(1);
    CHECK(g.out.text() == "+ctrl +shift +a -a -shift -ctrl", "a chord presses its modifiers first and releases the key first: %s", g.out.text().c_str());
}

static void testKeyHeldByAnotherSource() {
    // A key the macro lets go of stays down while a button binding holds it, and the end release does not touch it either.
    MacroRig g;
    g.apply("BUTTON_1", "key:shift");
    g.apply("BUTTON_2", "macro:+shift +a -a -shift");
    g.press(1);
    g.step();
    g.press(2);
    g.step();
    g.release(2);
    g.step(3);
    CHECK(g.out.keys.count(int(Key::Shift)) == 1, "shift is still held by the button: %s", g.out.text().c_str());
    CHECK(g.out.count("+shift") == 1 && g.out.count("-shift") == 0, "no second press and no early release: %s", g.out.text().c_str());
    g.release(1);
    g.step(2);
    CHECK(g.out.keys.empty(), "released with the button");
}

// ---------------------------------------------------------------- ignored triggers
static void testRetriggerIgnored() {
    MacroRig g;
    g.apply("BUTTON_3", "macro:+a wait:100 -a");
    g.tap(3);
    g.step(2);
    g.tap(3);  // while the macro runs
    g.tap(3);
    CHECK(g.out.count("+a") == 1, "triggers during a run are ignored: %s", g.out.text().c_str());
    g.step(40);
    CHECK(!g.runner.macroRunning() && g.out.keys.empty(), "the first run finished");
    CHECK(g.out.count("+a") == 1 && g.out.count("-a") == 1, "and left no queued second run: %s", g.out.text().c_str());
    g.tap(3);
    g.step(40);
    CHECK(g.out.count("+a") == 2 && g.out.count("-a") == 2, "after it is done the macro runs again: %s", g.out.text().c_str());
}

static void testTwoButtonsOneRunner() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:+a wait:100 -a");
    g.apply("BUTTON_2", "macro:+b -b");
    g.tap(1);
    g.tap(2);  // another macro button while the first runs: ignored too
    g.step(40);
    CHECK(g.out.count("+a") == 1 && g.out.count("+b") == 0, "one macro at a time: %s", g.out.text().c_str());
    g.tap(2);
    CHECK(g.out.count("+b") == 1, "the second runs once the first is done");
}

// ---------------------------------------------------------------- the 256 step queue
static void testStepLimit() {
    MacroRig g;
    g.apply("BUTTON_4", "macro:" + repeat("+a -a", 128));  // 256 steps
    g.tap(4);
    CHECK(g.out.count("+a") == 128 && g.out.count("-a") == 128 && g.out.log.size() == 256, "256 steps ran: %zu events", g.out.log.size());
    CHECK(!g.runner.macroRunning(), "and the macro is done in the tick");
    // 257 steps are refused by the parser and never reach the runner
    std::string err;
    Settings s;
    CHECK(!applyKey(s, "BUTTON_4", "macro:" + repeat("+a -a", 128) + " +b", &err), "257 steps refused: %s", err.c_str());
    // a worst case: 256 steps of four modifiers and a key each stay within what the output takes in one tick
    MacroRig h;
    h.apply("BUTTON_4", "macro:" + repeat("+ctrl+shift+alt+super+a -ctrl+shift+alt+super+a", 128));
    h.tap(4);
    CHECK(h.out.log.size() == 256 * 5 && h.out.keys.empty(), "5 key events per step: %zu", h.out.log.size());
}

// ---------------------------------------------------------------- release part of a button
static void testButtonReleasePart() {
    MacroRig g;
    g.apply("BUTTON_6", "macro:+a -a | +b -b");
    g.press(6);
    g.step(3);
    CHECK(g.out.text() == "+a -a", "the press part ran at the press: %s", g.out.text().c_str());
    g.release(6);
    g.step(2);
    CHECK(g.out.text() == "+a -a +b -b", "the release part ran at the release: %s", g.out.text().c_str());
    // a button without a release part runs nothing at the release
    MacroRig h;
    h.apply("BUTTON_6", "macro:+a -a");
    h.tap(6);
    CHECK(h.out.text() == "+a -a", "no release part, no release macro: %s", h.out.text().c_str());
    // only a release part
    MacroRig k;
    k.apply("BUTTON_6", "macro:none | +c -c");
    k.press(6);
    k.step(3);
    CHECK(k.out.log.empty(), "nothing at the press");
    k.release(6);
    k.step(2);
    CHECK(k.out.text() == "+c -c", "only the release part: %s", k.out.text().c_str());
    // a release part that comes while the press part still runs is ignored
    MacroRig m;
    m.apply("BUTTON_6", "macro:+a wait:200 -a | +b -b");
    m.tap(6);
    m.step(80);
    CHECK(m.out.count("+b") == 0 && m.out.count("-a") == 1, "the release part was ignored during the run: %s", m.out.text().c_str());
}

// ---------------------------------------------------------------- abort paths of the runner
static void startLongMacro(MacroRig &g) {
    g.apply("BUTTON_1", "macro:+ctrl +a wait:5000 -a -ctrl");
    g.tap(1);
    g.step(5);
    CHECK(g.runner.macroRunning() && g.out.keys.count(int(Key::Ctrl)) == 1 && g.out.keys.count(int(Key::A)) == 1, "the long macro holds ctrl and a");
}

static void expectAborted(MacroRig &g, const char *how) {
    CHECK(g.out.keys.empty(), "%s: nothing is held: %s", how, g.out.text().c_str());
    CHECK(!g.runner.macroRunning() && !g.runner.holdsAnything(), "%s: the macro does not run any more", how);
    const size_t n = g.out.log.size();
    g.step(1500);  // far beyond the pause: a surviving macro would act now
    CHECK(g.out.log.size() == n, "%s: the aborted macro did nothing more: %s", how, g.out.text().c_str());
    CHECK(g.out.keys.empty(), "%s: still nothing held", how);
}

static void testAbortPaths() {
    {
        MacroRig g;
        startLongMacro(g);
        g.runner.releaseAll();  // suspend, ENABLED = 0, safety net, "Release all keys", exit
        expectAborted(g, "releaseAll");
    }
    {
        MacroRig g;
        startLongMacro(g);
        ModeDef other;
        other.id = "other";
        g.runner.setMode(other);  // mode switch
        expectAborted(g, "setMode");
    }
    {
        MacroRig g;
        startLongMacro(g);
        g.runner.clearToggles();  // reload, device removal, "Release all keys"
        expectAborted(g, "clearToggles");
    }
    {
        // a macro that is aborted does not start its release part when the button is let go afterwards
        MacroRig g;
        g.apply("BUTTON_1", "macro:+a wait:5000 -a | +z -z");
        g.press(1);
        g.step(3);
        g.runner.releaseAll();
        g.release(1);
        g.step(5);
        CHECK(g.out.count("+z") == 0 && g.out.keys.empty(), "no release part after an abort: %s", g.out.text().c_str());
    }
}

static void testProfileSwitchKeepsMacro() {
    // resetDsp (another program got the focus) neither aborts the macro nor lets go of its modifiers
    MacroRig g;
    startLongMacro(g);
    g.runner.resetDsp();
    g.step(3);
    CHECK(g.runner.macroRunning() && g.out.keys.count(int(Key::Ctrl)) == 1, "the macro goes on: %s", g.out.text().c_str());
    CHECK(g.out.count("-ctrl") == 0 && g.out.count("+ctrl") == 1, "ctrl was not released and pressed again: %s", g.out.text().c_str());
    g.runner.releaseAll();
    CHECK(g.out.keys.empty(), "and ends with releaseAll");
}

static void testAbortThenRunAgain() {
    MacroRig g;
    startLongMacro(g);
    g.runner.releaseAll();
    g.apply("BUTTON_2", "macro:+b -b");
    g.tap(2);
    CHECK(g.out.count("+b") == 1 && g.out.count("-b") == 1, "a new macro runs after an abort: %s", g.out.text().c_str());
}

// ---------------------------------------------------------------- macros on the motions
struct MacroMotion {
    const char *name;
    const char *key;
    Axis axis;
    int raw;
    const char *extra;
};
static const MacroMotion kMacroMotions[] = {
    {"v_push", "V_PUSH_ACTION", AX_Z, 300, nullptr},
    {"v_tilt", "V_TILT_ACTION", AX_RX, 300, nullptr},
    {"h_push", "H_PUSH_ACTION", AX_X, 300, nullptr},
    {"h_tilt", "H_TILT_ACTION", AX_RZ, 300, nullptr},
    {"twist", "TWIST_ACTION", AX_RY, 300, nullptr},
    {"lift", "LIFT_ACTION", AX_Y, 300, "LIFT_DEADZONE"},
};

static void testMotionMacroEveryMotion() {
    for (const MacroMotion &m : kMacroMotions) {
        std::string first;
        for (int dir = -1; dir <= 1; dir += 2) {
            MacroRig g;
            g.apply(m.key, "macro:+a wait:20 -a,+b wait:20 -b");
            if (m.extra) g.apply(m.extra, "20");
            g.axis(m.axis, dir * m.raw);
            g.step(250);  // a second of deflection
            const int na = g.out.count("+a"), nb = g.out.count("+b");
            CHECK(na + nb == 1, "%s dir %d: one macro per crossing, got a=%d b=%d (%s)", m.name, dir, na, nb, g.out.text().c_str());
            CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "%s dir %d: nothing held at the end", m.name, dir);
            const std::string which = na ? "a" : "b";
            if (dir == -1) first = which;
            else CHECK(which != first, "%s: the two directions run different macros", m.name);
        }
    }
}

static void testMotionMacroNoneSide() {
    for (const MacroMotion &m : kMacroMotions) {
        int fired = 0;
        for (int dir = -1; dir <= 1; dir += 2) {
            MacroRig g;
            g.apply(m.key, "macro:none,+b -b");
            if (m.extra) g.apply(m.extra, "20");
            g.axis(m.axis, dir * m.raw);
            g.step(100);
            fired += g.out.count("+b");
        }
        CHECK(fired == 1, "%s: none on one side, a macro on the other (%d)", m.name, fired);
    }
}

static void testMotionOnePerCrossing() {
    MacroRig g;
    g.apply("H_PUSH_ACTION", "macro:+a -a,+a -a");
    for (int i = 0; i < 3; ++i) {
        g.axis(AX_X, 300);
        g.step(50);
        g.axis(AX_X, 0);
        g.step(50);
    }
    CHECK(g.out.count("+a") == 3, "three crossings, three runs: %zu events", g.out.log.size());
    // a wobble that stays above half of the trigger fires nothing more
    MacroRig h;
    h.apply("H_PUSH_ACTION", "macro:+a -a,+a -a");
    for (int i = 0; i < 6; ++i) {
        h.axis(AX_X, i % 2 ? 300 : 200);
        h.step(50);
    }
    CHECK(h.out.count("+a") == 1, "a wobble above half the level fires once: %d", h.out.count("+a"));
}

static void testMotionReleasePart() {
    MacroRig g;
    g.apply("H_PUSH_ACTION", "macro:+a -a | +c -c,+b -b | +d -d");
    // find out which side a positive push is
    g.axis(AX_X, 300);
    g.step(60);
    const bool posIsB = g.out.count("+b") == 1;
    CHECK(g.out.count("+a") + g.out.count("+b") == 1 && g.out.count("+c") == 0 && g.out.count("+d") == 0, "no release part while deflected: %s", g.out.text().c_str());
    g.axis(AX_X, 0);
    g.step(60);
    CHECK(posIsB ? g.out.count("+d") == 1 : g.out.count("+c") == 1, "the release part of that side ran when the motion came back: %s", g.out.text().c_str());
    CHECK(g.out.count("+c") + g.out.count("+d") == 1 && g.out.keys.empty(), "only that one: %s", g.out.text().c_str());
}

static void testMotionTriggerIgnoredWhileRunning() {
    MacroRig g;
    g.apply("H_PUSH_ACTION", "macro:+a wait:1000 -a,+a wait:1000 -a");
    for (int i = 0; i < 4; ++i) {  // cross the trigger four times within the run
        g.axis(AX_X, 300);
        g.step(20);
        g.axis(AX_X, 0);
        g.step(20);
    }
    CHECK(g.out.count("+a") == 1, "re-triggers during the run are ignored: %s", g.out.text().c_str());
}

static void testMotionAbort() {
    MacroRig g;
    g.apply("H_PUSH_ACTION", "macro:+a wait:5000 -a,+a wait:5000 -a");
    g.axis(AX_X, 300);
    g.step(20);
    CHECK(g.runner.macroRunning() && g.out.keys.count(int(Key::A)) == 1, "running");
    g.axis(AX_X, 0);
    g.runner.releaseAll();
    expectAborted(g, "motion macro releaseAll");
}

static void testMacroMixedWithHold() {
    // A macro key and a hold on the same key: it stays down until both let go.
    MacroRig g;
    g.apply("H_PUSH_ACTION", "hold:shift,shift");
    g.apply("BUTTON_1", "macro:+shift wait:40 -shift");
    g.axis(AX_X, 300);
    g.step(20);
    g.tap(1);
    g.step(30);
    CHECK(g.out.keys.count(int(Key::Shift)) == 1, "the hold keeps shift down after the macro ended: %s", g.out.text().c_str());
    g.axis(AX_X, 0);
    g.step(30);
    CHECK(g.out.keys.empty(), "released with the hold");
}

int main() {
    testButtonOrderAndTiming();
    testExactPauseTiming();
    testTrailingPause();
    testPressOnlyMacro();
    testChordOrder();
    testKeyHeldByAnotherSource();
    testRetriggerIgnored();
    testTwoButtonsOneRunner();
    testStepLimit();
    testButtonReleasePart();
    testAbortPaths();
    testProfileSwitchKeepsMacro();
    testAbortThenRunAgain();
    testMotionMacroEveryMotion();
    testMotionMacroNoneSide();
    testMotionOnePerCrossing();
    testMotionReleasePart();
    testMotionTriggerIgnoredWhileRunning();
    testMotionAbort();
    testMacroMixedWithHold();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_macro_runner OK");
    return 0;
}
