// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Stages 2 to 4 of docs/plan-macro-engine.md: the tap step, the mouse steps, repeat blocks, the time bound, the while_held prefix
// (parser, canonical text, risk text and the runner with a fake output) and a small benchmark of the trigger path.
#undef NDEBUG
#include <cassert>
#include <chrono>
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

// Logs key events ("+a", "-a", "tap:a") and mouse events ("m+1", "m-1", 1 left, 2 middle, 3 right).
struct MacroRec : Output {
    std::vector<std::string> log;
    std::set<int> keys, mouse;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key k, bool d) override {
        if (d) keys.insert(int(k)); else keys.erase(int(k));
        log.push_back(std::string(d ? "+" : "-") + keyName(k));
    }
    void tapKey(Key k) override { log.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override {
        if (d) mouse.insert(b); else mouse.erase(b);
        log.push_back(std::string(d ? "m+" : "m-") + std::to_string(b));
    }
    void releaseAll() override { keys.clear(); mouse.clear(); }
    void flush() override {}
    int count(const std::string &e) const {
        int n = 0;
        for (const std::string &x : log) n += x == e;
        return n;
    }
    std::string text() const {
        std::string t;
        for (const std::string &x : log) t += (t.empty() ? "" : " ") + x;
        return t;
    }
};

struct MacroRig {
    MacroRec out;
    ModeRunner runner;
    Settings s;
    uint32_t buttons = 0;
    RawAxes raw{};
    static constexpr double kDt = 1.0 / 250.0;
    MacroRig() : runner(&out) {
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
        for (int i = 0; i < n; ++i) runner.tick(s, raw, buttons, true, kDt);
    }
    void press(int b) { buttons |= 1u << (b - 1); }
    void release(int b) { buttons &= ~(1u << (b - 1)); }
};

// ------------------------------------------------------------------------------------------------------------- parser
static void testParseNewSteps() {
    MacroStep st;
    std::string err;
    CHECK(parseMacroStep("tap:ctrl+c", &st, &err) && st.kind == MacroStep::Kind::Tap && st.key == Key::C && st.mods.n == 1, "tap chord: %s", err.c_str());
    CHECK(macroStepText(st) == "tap:ctrl+c", "tap text");
    CHECK(parseMacroStep("TAP:A", &st, &err) && macroStepText(st) == "tap:a", "tap folds case");
    CHECK(!parseMacroStep("tap:", &st, &err), "tap needs a key");
    CHECK(!parseMacroStep("tap:nonsense", &st, &err) && err.find("unknown key") != std::string::npos, "tap: unknown key: %s", err.c_str());
    for (const char *n : {"click", "rightclick", "middleclick", "+click", "-click", "+rightclick", "-middleclick"}) {
        CHECK(parseMacroStep(n, &st, &err) && macroStepText(st) == n, "mouse step %s round trips", n);
    }
    CHECK(parseMacroStep("click", &st, &err) && st.kind == MacroStep::Kind::MouseTap && st.mouse == 1, "click is the left button");
    CHECK(parseMacroStep("rightclick", &st, &err) && st.mouse == 3, "rightclick is button 3");
    CHECK(parseMacroStep("middleclick", &st, &err) && st.mouse == 2, "middleclick is button 2");
    CHECK(parseMacroStep("repeat:3", &st, &err) && st.kind == MacroStep::Kind::RepeatBegin && st.count == 3, "repeat:3");
    CHECK(macroStepText(st) == "repeat:3", "repeat text");
    CHECK(parseMacroStep("end", &st, &err) && st.kind == MacroStep::Kind::RepeatEnd, "end");
    CHECK(!parseMacroStep("repeat:0", &st, &err), "repeat:0 refused");
    CHECK(!parseMacroStep("repeat:1001", &st, &err), "repeat:1001 refused");
    CHECK(!parseMacroStep("repeat:x", &st, &err), "repeat:x refused");
    CHECK(parseMacroStep("repeat:1000", &st, &err) && st.count == 1000, "repeat:1000 allowed");
    // the old forms keep their meaning
    CHECK(parseMacroStep("+ctrl+c", &st, &err) && st.kind == MacroStep::Kind::Down, "+chord");
    CHECK(parseMacroStep("-a", &st, &err) && st.kind == MacroStep::Kind::Up, "-key");
    CHECK(parseMacroStep("wait:30", &st, &err) && st.waitMs == 30, "wait");
    CHECK(!parseMacroStep("+bogus", &st, &err), "a key is still checked");
}

static void testRepeatStructure() {
    MacroSteps v;
    std::string err;
    CHECK(parseMacroSteps("tap:x repeat:3 tap:a wait:10 end tap:y", &v, &err) && v.size() == 6, "a block with steps around it: %s", err.c_str());
    CHECK(macroStepsText(v) == "tap:x repeat:3 tap:a wait:10 end tap:y", "canonical text: %s", macroStepsText(v).c_str());
    CHECK(macroExpandedSize(v) == 1 + 6 + 1, "expanded size %zu", macroExpandedSize(v));
    CHECK(macroStepsProblem(v).empty(), "valid in memory");
    CHECK(!parseMacroSteps("repeat:2 repeat:2 +a end end", &v, &err) && err.find("nested") != std::string::npos, "nesting refused: %s", err.c_str());
    CHECK(!parseMacroSteps("+a end", &v, &err) && err.find("end without repeat") != std::string::npos, "end alone: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat:2 +a", &v, &err) && err.find("without end") != std::string::npos, "no end: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat:2 end", &v, &err) && err.find("at least one step") != std::string::npos, "empty block: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat +a end", &v, &err) && err.find("while_held") != std::string::npos, "no count without while_held: %s", err.c_str());
    CHECK(parseMacroSteps("repeat +a end", &v, &err, true), "no count allowed when asked: %s", err.c_str());
    CHECK(!macroStepsProblem(v).empty() && macroStepsProblem(v, true).empty(), "the problem check follows allowInfinite");
    // 256 steps after expansion
    CHECK(parseMacroSteps("repeat:128 tap:a wait:1 end", &v, &err) && macroExpandedSize(v) == 256, "256 expanded: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat:129 tap:a wait:1 end", &v, &err) && err.find("too many steps") != std::string::npos, "258 expanded refused: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat:1000 tap:a end", &v, &err), "1000 passes of one step refused: %s", err.c_str());
    CHECK(parseMacroSteps("repeat:256 tap:a end", &v, &err), "256 passes of one step allowed: %s", err.c_str());
    CHECK(!parseMacroSteps("repeat:256 tap:a end tap:b", &v, &err), "257 expanded with a step after it refused");
    MacroSteps mem;
    for (int i = 0; i < 200; ++i) mem.push_back(MacroStep::tap(Key::A));
    mem.push_back(MacroStep::repeatBegin(2));
    mem.push_back(MacroStep::tap(Key::B));
    mem.push_back(MacroStep::tap(Key::B));
    mem.push_back(MacroStep::repeatEnd());
    CHECK(macroStepsProblem(mem).empty() && macroExpandedSize(mem) == 204, "in-memory list: %s", macroStepsProblem(mem).c_str());
    mem.push_back(MacroStep::repeatBegin(60));
    mem.push_back(MacroStep::tap(Key::B));
    mem.push_back(MacroStep::repeatEnd());
    CHECK(!macroStepsProblem(mem).empty(), "in-memory list above the limit: %zu", macroExpandedSize(mem));
}

static void testSideWhileHeld() {
    MacroSide side;
    std::string err;
    CHECK(parseMacroSide("while_held +a wait:100 -a | tap:b", &side, &err) && side.whileHeld && side.press.size() == 3 && side.release.size() == 1, "parsed: %s", err.c_str());
    CHECK(macroSideText(side) == "while_held +a wait:100 -a | tap:b", "text: %s", macroSideText(side).c_str());
    MacroSide again;
    CHECK(parseMacroSide(macroSideText(side), &again, &err) && again == side, "round trip");
    CHECK(parseMacroSide("WHILE_HELD   tap:a", &side, &err) && side.whileHeld && macroSideText(side) == "while_held tap:a", "case and blanks");
    CHECK(parseMacroSide("tap:a", &side, &err) && !side.whileHeld, "default is off");
    MacroSide off, on;
    CHECK(parseMacroSide("tap:a", &off, &err) && parseMacroSide("while_held tap:a", &on, &err) && !(off == on), "operator== sees the flag");
    CHECK(!parseMacroSide("while_held", &side, &err) && err.find("while_held") != std::string::npos, "while_held alone: %s", err.c_str());
    CHECK(!parseMacroSide("while_held none | tap:a", &side, &err), "while_held needs a press part: %s", err.c_str());
    CHECK(parseMacroSide("while_held repeat tap:a end", &side, &err) && side.press[0].count == 0, "infinite repeat with while_held: %s", err.c_str());
    CHECK(!parseMacroSide("tap:a | repeat tap:a end", &side, &err), "infinite repeat never in the release part");
    CHECK(!parseMacroSide("repeat tap:a end", &side, &err), "infinite repeat needs while_held");
    CHECK(parseMacroSide("while_held_x +a", &side, &err) == false, "a longer word is not the prefix");
    // through the button and motion parsers
    ButtonActionType t;
    Key k;
    MacroSide bs;
    CHECK(parseButtonAction("macro:while_held +a -a", &t, &k, &err, nullptr, nullptr, &bs) && t == ButtonActionType::Macro && bs.whileHeld, "button: %s", err.c_str());
    ChannelAction a;
    CHECK(parseChannelAction("macro:while_held +a -a,none", &a, &err) && a.macro->neg.whileHeld && !a.macro->pos.whileHeld, "motion: %s", err.c_str());
    CHECK(channelActionText(a) == "macro:while_held +a -a,none", "motion text: %s", channelActionText(a).c_str());
}

static void testRisk() {
    MacroSteps v;
    std::string err;
    CHECK(parseMacroSteps("tap:ctrl+c tap:left", &v, &err), "%s", err.c_str());
    CHECK(macroRiskText(v) == "ctrl+c", "a tap counts like a down: %s", macroRiskText(v).c_str());
    CHECK(parseMacroSteps("click +rightclick", &v, &err) && macroRiskText(v) == "click, rightclick", "mouse clicks are named: %s", macroRiskText(v).c_str());
    CHECK(parseMacroSteps("-click -rightclick", &v, &err) && macroRiskText(v).empty(), "a release alone is not named");
}

// -------------------------------------------------------------------------------------------------------------- runner
static void testTapAndMouse() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:tap:ctrl+c click rightclick middleclick");
    g.press(1);
    g.step();
    CHECK(g.out.text() == "+ctrl tap:c -ctrl m+1 m-1 m+3 m-3 m+2 m-2", "tap and clicks: %s", g.out.text().c_str());
    CHECK(g.out.keys.empty() && g.out.mouse.empty(), "nothing held");
}

static void testMouseHold() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:+click wait:2000 -click");
    g.press(1);
    g.step(10);
    CHECK(g.out.mouse.count(1) == 1 && g.runner.macroRunning(), "the button is held during the pause");
    g.release(1);
    g.step(600);  // 2.4 s: the pause is over
    CHECK(g.out.mouse.empty() && g.out.count("m+1") == 1 && g.out.count("m-1") == 1, "released by the step: %s", g.out.text().c_str());
    // press-only mouse step is released when the macro ends
    MacroRig h;
    h.apply("BUTTON_1", "macro:+rightclick wait:100");
    h.press(1);
    h.step(10);
    CHECK(h.out.mouse.count(3) == 1, "held");
    h.step(40);
    CHECK(h.out.mouse.empty() && !h.runner.macroRunning(), "released at the end: %s", h.out.text().c_str());
    // an abort releases it
    MacroRig a;
    a.apply("BUTTON_1", "macro:+click +middleclick wait:5000 -click");
    a.press(1);
    a.step(10);
    CHECK(a.out.mouse.size() == 2, "two held");
    a.runner.clearToggles();
    a.step(1);
    CHECK(a.out.mouse.empty() && !a.runner.macroRunning(), "clearToggles released the mouse buttons: %s", a.out.text().c_str());
    MacroRig r;
    r.apply("BUTTON_1", "macro:+click wait:5000 -click");
    r.press(1);
    r.step(10);
    r.runner.releaseAll();
    CHECK(r.out.mouse.empty(), "releaseAll released the mouse button");
}

static void testRepeatRuns() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:tap:x repeat:3 tap:a end tap:y");
    g.press(1);
    g.step(20);
    CHECK(g.out.text() == "tap:x tap:a tap:a tap:a tap:y", "parts that repeat and parts that do not: %s", g.out.text().c_str());
    CHECK(!g.runner.macroRunning(), "done");
    // a pass takes at least one tick, even without a pause
    MacroRig h;
    h.apply("BUTTON_1", "macro:repeat:10 tap:a end");
    h.press(1);
    h.step(1);
    CHECK(h.out.count("tap:a") == 1 && h.runner.macroRunning(), "one pass per tick: %d", h.out.count("tap:a"));
    h.step(5);
    CHECK(h.out.count("tap:a") == 6, "six ticks, six passes: %d", h.out.count("tap:a"));
    h.step(10);
    CHECK(h.out.count("tap:a") == 10 && !h.runner.macroRunning(), "ten in all: %d", h.out.count("tap:a"));
    // with pauses
    MacroRig p;
    p.apply("BUTTON_1", "macro:repeat:4 +a wait:20 -a wait:20 end");
    p.press(1);
    p.step(400);
    CHECK(p.out.count("+a") == 4 && p.out.count("-a") == 4 && p.out.keys.empty(), "4 passes with pauses: %s", p.out.text().c_str());
    // 1000 passes through the longest allowed text
    MacroRig m;
    m.apply("BUTTON_1", "macro:repeat:256 tap:a end");
    m.press(1);
    m.step(300);
    CHECK(m.out.count("tap:a") == 256, "256 passes: %d", m.out.count("tap:a"));
}

static void testRepeatAbort() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:repeat:60 +a wait:50 -a wait:50 end");
    g.press(1);
    g.step(30);
    CHECK(g.runner.macroRunning(), "running");
    g.runner.releaseAll();
    CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "abort inside a block: %s", g.out.text().c_str());
    const size_t n = g.out.log.size();
    g.step(1000);
    CHECK(g.out.log.size() == n, "nothing more after the abort");
}

static void testWhileHeldButton() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:while_held +a wait:5000 -a | tap:z");
    g.press(1);
    g.step(20);
    CHECK(g.out.keys.count(int(Key::A)) == 1 && g.runner.macroRunning(), "press part holds a");
    g.release(1);
    g.step(1);
    CHECK(g.out.keys.empty(), "released at once: %s", g.out.text().c_str());
    CHECK(g.out.count("tap:z") == 1, "the release part ran: %s", g.out.text().c_str());
    CHECK(g.out.text().find("-a") < g.out.text().find("tap:z"), "first the release of a, then the release part: %s", g.out.text().c_str());
    g.step(2000);
    CHECK(g.out.keys.empty() && !g.runner.macroRunning() && g.out.count("+a") == 1 && g.out.count("tap:z") == 1, "no more events: %s", g.out.text().c_str());
    // the same without the prefix: the release does not stop it (today's behavior), and the release part is dropped
    MacroRig h;
    h.apply("BUTTON_1", "macro:+a wait:1000 -a | tap:z");
    h.press(1);
    h.step(20);
    h.release(1);
    h.step(1);
    CHECK(h.out.keys.count(int(Key::A)) == 1 && h.runner.macroRunning(), "default: still running after the release");
    h.step(400);
    CHECK(h.out.keys.empty() && h.out.count("tap:z") == 0, "default: ran to its end, release part dropped: %s", h.out.text().c_str());
    // a press part that ended by itself: nothing to stop, the release part runs as usual
    MacroRig e;
    e.apply("BUTTON_1", "macro:while_held tap:a | tap:z");
    e.press(1);
    e.step(10);
    e.release(1);
    e.step(10);
    CHECK(e.out.text() == "tap:a tap:z", "short macro: %s", e.out.text().c_str());
    // held modifiers and the mouse go too
    MacroRig m;
    m.apply("BUTTON_1", "macro:while_held +ctrl+shift +click wait:5000");
    m.press(1);
    m.step(10);
    CHECK(m.out.keys.size() == 2 && m.out.mouse.size() == 1, "holds ctrl, shift and the left button");
    m.release(1);
    m.step(1);
    CHECK(m.out.keys.empty() && m.out.mouse.empty() && !m.runner.macroRunning(), "all released: %s", m.out.text().c_str());
}

static void testWhileHeldRepeat() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:while_held repeat tap:a end | tap:z");
    g.press(1);
    g.step(100);
    const int n = g.out.count("tap:a");
    CHECK(n >= 90 && n <= 101, "repeats while held: %d", n);
    g.release(1);
    g.step(1);
    CHECK(g.out.count("tap:z") == 1 && !g.runner.macroRunning(), "stops and runs the release part: %s", g.out.text().c_str());
    const int m = g.out.count("tap:a");
    g.step(100);
    CHECK(g.out.count("tap:a") == m, "no more taps after the release");
    // a pause inside the block
    MacroRig h;
    h.apply("BUTTON_1", "macro:while_held repeat +b wait:40 -b wait:40 end");
    h.press(1);
    h.step(5);  // inside the first pause (20 ms of 40)
    CHECK(h.out.keys.count(int(Key::B)) == 1, "b is down in the pause");
    h.release(1);
    h.step(1);
    CHECK(h.out.keys.empty() && !h.runner.macroRunning(), "abort in a pause: b is up: %s", h.out.text().c_str());
    // between a down and its up: the key is up, not stuck
    MacroRig u;
    u.apply("BUTTON_1", "macro:while_held repeat +c +d -d -c wait:10 end");
    u.press(1);
    u.step(7);
    u.release(1);
    u.step(3);
    CHECK(u.out.keys.empty(), "no key left down");
}

static void testTimeBound() {
    MacroRig g;
    g.apply("BUTTON_1", "macro:while_held repeat +a wait:1000 -a wait:1000 end");
    g.press(1);
    g.step(250 * 590);  // 590 s
    CHECK(g.runner.macroRunning(), "still running at 590 s");
    g.step(250 * 20);   // 610 s, the button is still held
    CHECK(!g.runner.macroRunning() && g.out.keys.empty(), "ended at 10 minutes, nothing held: %s", g.out.keys.empty() ? "" : "key held");
    const size_t n = g.out.log.size();
    g.step(2000);
    CHECK(g.out.log.size() == n, "and it does not start again while the button is held");
    g.release(1);
    g.step(10);
    // a normal long macro (60 s pauses) is cut as well
    MacroRig h;
    std::string longMacro = "macro:+a";
    for (int i = 0; i < 12; ++i) longMacro += " wait:60000";
    h.apply("BUTTON_1", longMacro);
    h.press(1);
    h.step(250 * 700);
    CHECK(!h.runner.macroRunning() && h.out.keys.empty(), "a macro of 12 minutes is cut at 10");
}

static void testWhileHeldMotion() {
    MacroRig g;
    g.apply("H_PUSH_ACTION", "macro:while_held +a wait:5000 -a | tap:c,while_held +b wait:5000 -b | tap:d");
    g.raw[AX_X] = 300;
    g.step(60);
    const bool posIsB = g.out.keys.count(int(Key::B)) == 1;
    CHECK(g.out.keys.size() == 1 && g.runner.macroRunning(), "a side holds its key while deflected: %s", g.out.text().c_str());
    g.raw[AX_X] = 0;
    g.step(60);
    CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "released when the motion fell back: %s", g.out.text().c_str());
    CHECK(g.out.count(posIsB ? "tap:d" : "tap:c") == 1, "the release part of that side ran: %s", g.out.text().c_str());
    const size_t n = g.out.log.size();
    g.step(2000);
    CHECK(g.out.log.size() == n, "nothing more");
    // without the prefix the macro runs on
    MacroRig h;
    h.apply("H_PUSH_ACTION", "macro:+a wait:1000 -a,+b wait:1000 -b");
    h.raw[AX_X] = 300;
    h.step(60);
    h.raw[AX_X] = 0;
    h.step(5);
    CHECK(h.out.keys.size() == 1 && h.runner.macroRunning(), "default: it keeps running after the motion is back");
}

static void testWhileHeldOwnership() {
    // button 2 releases while a macro of button 1 runs: it is not stopped
    MacroRig g;
    g.apply("BUTTON_1", "macro:while_held +a wait:5000 -a");
    g.apply("BUTTON_2", "macro:while_held +b wait:5000 -b");
    g.press(1);
    g.step(10);
    g.press(2);  // ignored: one macro at a time
    g.step(10);
    g.release(2);
    g.step(10);
    CHECK(g.runner.macroRunning() && g.out.keys.count(int(Key::A)) == 1, "the macro of button 1 goes on: %s", g.out.text().c_str());
    g.release(1);
    g.step(2);
    CHECK(g.out.keys.empty() && !g.runner.macroRunning(), "and ends with its own button");
}

// ------------------------------------------------------------------------------------------------------------ limits
static void testLimitsAndText() {
    CHECK(kMaxActionTextBytes == 256u * 1024u, "action text 256 KiB");
    CHECK(kMaxLineBytes > kMaxActionTextBytes, "a line holds an action");
    CHECK(kMaxMacroRepeat == 1000 && kMaxMacroRunSeconds == 600, "repeat and time bounds");
    // a 256-step macro of 5-char steps is far below the text limit
    std::string t = "macro:";
    for (int i = 0; i < 128; ++i) t += "+a -a ";
    std::string err;
    Settings s;
    CHECK(applyKey(s, "BUTTON_3", t, &err), "256 steps: %s", err.c_str());
    CHECK(applyKey(s, "BUTTON_4", "macro:while_held repeat tap:ctrl+c click end | tap:x", &err), "new forms through applyKey: %s", err.c_str());
    CHECK(s.buttons.at(4) == "macro:while_held repeat tap:ctrl+c click end | tap:x", "canonical: %s", s.buttons.at(4).c_str());
}

// ------------------------------------------------------------------------------------------------------- benchmark
// Many triggers of a long macro through one runner: no copy per trigger and no parse per edge. The bound is generous (a regression
// to a vector copy and a parse per press is a factor of many); the time is printed for a human.
static void testBenchmark() {
    MacroRig g;
    std::string t = "macro:";
    for (int i = 0; i < 128; ++i) t += "+a -a ";
    g.apply("BUTTON_1", t);
    for (int b = 2; b <= 12; ++b) g.apply("BUTTON_" + std::to_string(b), "key:ctrl+" + std::string(1, char('a' + b)));
    const int triggers = 3000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < triggers; ++i) {
        g.press(1);
        g.step();
        g.release(1);
        g.step();
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("benchmark: %d triggers of a 256-step macro, %.1f ms (%.1f us per trigger)\n", triggers, ms, ms * 1000.0 / triggers);
    CHECK(g.out.count("+a") == triggers * 128, "every trigger ran all its steps: %d", g.out.count("+a"));
    CHECK(ms < 5000.0, "the trigger path is cheap: %.1f ms", ms);
}

int main() {
    testParseNewSteps();
    testRepeatStructure();
    testSideWhileHeld();
    testRisk();
    testTapAndMouse();
    testMouseHold();
    testRepeatRuns();
    testRepeatAbort();
    testWhileHeldButton();
    testWhileHeldRepeat();
    testTimeBound();
    testWhileHeldMotion();
    testWhileHeldOwnership();
    testLimitsAndText();
    testBenchmark();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_macro_engine: ok");
    return 0;
}
