// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The macro function (stage 7b of docs/plan-motion-functions.md): the toolkit-free model (src/macro.hpp: steps, sides, text round
// trips, limits, messages) and its use by the action parsers (parseButtonAction, parseChannelAction), the settings and the mode
// files. The runner (timing, release at the end, abort paths) is in tests/test_macro_runner.cpp.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "macro.hpp"
#include "modes.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static std::string repeatSteps(const std::string &step, size_t n) {
    std::string t;
    for (size_t i = 0; i < n; ++i) t += (i ? " " : "") + step;
    return t;
}

// ---------------------------------------------------------------- one step
static void testMacroSteps() {
    MacroStep s;
    std::string err;
    CHECK(parseMacroStep("+ctrl+c", &s, &err) && s.kind == MacroStep::Kind::Down && s.key == Key::C && s.mods.n == 1 &&
              s.mods.k[0] == Key::Ctrl,
          "down chord: %s", err.c_str());
    CHECK(macroStepText(s) == "+ctrl+c", "text of a down chord: %s", macroStepText(s).c_str());
    CHECK(parseMacroStep("-A", &s, &err) && s.kind == MacroStep::Kind::Up && s.key == Key::A && s.mods.n == 0, "up, case folded");
    CHECK(macroStepText(s) == "-a", "text of an up step");
    CHECK(parseMacroStep("+shift", &s, &err) && s.key == Key::Shift && s.mods.n == 0, "a lone modifier is a step");
    CHECK(parseMacroStep("+ctrl+shift", &s, &err) && s.key == Key::Shift && s.mods.n == 1, "modifiers only: %s", err.c_str());
    CHECK(macroStepText(s) == "+ctrl+shift", "modifiers-only text: %s", macroStepText(s).c_str());
    CHECK(parseMacroStep("+super+alt+tab", &s, &err) && s.key == Key::Tab && s.mods.n == 2, "two modifiers and a key");
    CHECK(macroStepText(s) == "+super+alt+tab", "order as written: %s", macroStepText(s).c_str());
    CHECK(parseMacroStep("wait:30", &s, &err) && s.kind == MacroStep::Kind::Wait && s.waitMs == 30, "wait");
    CHECK(macroStepText(s) == "wait:30", "wait text");
    CHECK(parseMacroStep("wait:1", &s, nullptr) && s.waitMs == 1, "the smallest pause");
    CHECK(parseMacroStep("wait:60000", &s, nullptr) && s.waitMs == 60000, "the largest pause");
    CHECK(!parseMacroStep("wait:0", &s, &err) && err.find("1..60000") != std::string::npos, "a pause of 0 refused: %s", err.c_str());
    CHECK(!parseMacroStep("wait:60001", &s, &err) && err.find("1..60000") != std::string::npos, "a pause above the limit refused: %s", err.c_str());
    CHECK(!parseMacroStep("wait:", &s, &err), "wait without a number");
    CHECK(!parseMacroStep("wait:-5", &s, &err), "a negative pause");
    CHECK(!parseMacroStep("wait:1.5", &s, &err), "a fraction of a millisecond");
    CHECK(!parseMacroStep("wait:100000", &s, &err), "six digits");
    CHECK(!parseMacroStep("wait:+3", &s, &err), "a sign");
    CHECK(!parseMacroStep("a", &s, &err) && err.find("+<key>") != std::string::npos, "a bare key is not a step (no tap step): %s", err.c_str());
    CHECK(!parseMacroStep("tap:a", &s, &err), "there is no tap step");
    CHECK(!parseMacroStep("+", &s, &err), "a plus alone");
    CHECK(!parseMacroStep("+nonsense", &s, &err) && err.find("unknown key") != std::string::npos, "unknown key: %s", err.c_str());
    CHECK(!parseMacroStep("+ctrl+ctrl+a", &s, &err) && err.find("twice") != std::string::npos, "a modifier twice: %s", err.c_str());
    CHECK(!parseMacroStep("+a+b", &s, &err), "two keys in a chord");
    CHECK(!parseMacroStep("+ctrl+shift+alt+super+ctrl", &s, &err), "five modifiers");
    CHECK(!parseMacroStep("", &s, &err), "empty");
    // the keys of a step, in the order they are pressed
    Key ks[kMaxMacroKeysPerStep];
    CHECK(parseMacroStep("+ctrl+shift+a", &s, nullptr) && macroStepKeys(s, ks) == 3 && ks[0] == Key::Ctrl && ks[1] == Key::Shift && ks[2] == Key::A,
          "keys of a chord");
    CHECK(macroStepKeys(MacroStep::wait(5), ks) == 0, "a pause has no keys");
    // every key name of the closed list works in a step and round trips
    for (size_t i = 0; i < kKeyCount; ++i) {
        const std::string name = keyName(static_cast<Key>(i));
        CHECK(parseMacroStep("+" + name, &s, &err) && s.key == static_cast<Key>(i) && macroStepText(s) == "+" + name, "key %s: %s", name.c_str(), err.c_str());
    }
}

// ---------------------------------------------------------------- a list of steps
static void testMacroStepLists() {
    MacroSteps v;
    std::string err;
    CHECK(parseMacroSteps("+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b", &v, &err) && v.size() == 14, "the plan's example: %s", err.c_str());
    CHECK(macroStepsText(v) == "+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b", "round trip: %s", macroStepsText(v).c_str());
    CHECK(parseMacroSteps("  +A \t -a   WAIT:5 ", &v, &err) && macroStepsText(v) == "+a -a wait:5", "spaces, tabs and case: %s", macroStepsText(v).c_str());
    CHECK(parseMacroSteps("", &v, &err) && v.empty(), "empty text is an empty list");
    CHECK(parseMacroSteps("none", &v, &err) && v.empty(), "none is an empty list");
    CHECK(macroStepsText(MacroSteps()) == "none", "an empty list is written as none");
    CHECK(parseMacroSteps("+ctrl", &v, &err) && v.size() == 1, "a press-only macro is valid");
    CHECK(parseMacroSteps("+shift wait:100", &v, &err) && v.size() == 2, "press and pause, never released by a step: valid");
    CHECK(!parseMacroSteps("+a none", &v, &err), "none is not a step");
    CHECK(!parseMacroSteps("+a -b +zzz", &v, &err) && err.find("step 3") != std::string::npos && err.find("+zzz") != std::string::npos,
          "the message names the step: %s", err.c_str());
    // the count limit: exactly 256 is fine, 257 is not (the plan: "the 257th macro step is rejected")
    CHECK(parseMacroSteps(repeatSteps("wait:1", kMaxMacroSteps), &v, &err) && v.size() == kMaxMacroSteps, "256 steps: %s", err.c_str());
    CHECK(!parseMacroSteps(repeatSteps("wait:1", kMaxMacroSteps + 1), &v, &err) && err.find("too many steps") != std::string::npos &&
              err.find("256") != std::string::npos,
          "257 steps: %s", err.c_str());
    // validation of a list built in memory (the dialog)
    MacroSteps m;
    for (size_t i = 0; i < kMaxMacroSteps; ++i) m.push_back(MacroStep::down(Key::A));
    CHECK(macroStepsProblem(m).empty(), "256 steps are valid");
    m.push_back(MacroStep::up(Key::A));
    CHECK(macroStepsProblem(m).find("257") != std::string::npos, "257 steps: %s", macroStepsProblem(m).c_str());
    CHECK(macroStepsProblem(MacroSteps()).empty(), "an empty list is valid for the list");
    CHECK(macroStepsProblem({MacroStep::wait(0)}).find("step 1") != std::string::npos, "pause 0: %s", macroStepsProblem({MacroStep::wait(0)}).c_str());
    CHECK(macroStepsProblem({MacroStep::down(Key::A), MacroStep::wait(60001)}).find("step 2") != std::string::npos, "pause 60001");
    CHECK(macroStepsProblem({MacroStep::wait(1), MacroStep::wait(60000)}).empty(), "the pause limits themselves are valid");
    CHECK(kMaxMacroSteps == 256 && kMinMacroWaitMs == 1 && kMaxMacroWaitMs == 60000 && kDefaultMacroWaitMs == 30, "the documented constants");
    // equality
    CHECK(MacroStep::down(Key::A) == MacroStep::down(Key::A) && !(MacroStep::down(Key::A) == MacroStep::up(Key::A)) &&
              !(MacroStep::wait(1) == MacroStep::wait(2)),
          "step equality");
}

// ---------------------------------------------------------------- sides and motion actions
static void testMacroSides() {
    MacroSide side;
    std::string err;
    CHECK(parseMacroSide("+ctrl +c -c -ctrl", &side, &err) && side.press.size() == 4 && side.release.empty(), "a side with a press part: %s", err.c_str());
    CHECK(macroSideText(side) == "+ctrl +c -c -ctrl", "text");
    CHECK(parseMacroSide("+a | -a", &side, &err) && side.press.size() == 1 && side.release.size() == 1, "press and release parts: %s", err.c_str());
    CHECK(macroSideText(side) == "+a | -a", "canonical text with spaces around the bar: %s", macroSideText(side).c_str());
    CHECK(parseMacroSide("+a|-a", &side, &err) && macroSideText(side) == "+a | -a", "no spaces around the bar are accepted");
    CHECK(parseMacroSide("none | +a -a", &side, &err) && side.press.empty() && side.release.size() == 2, "only a release part: %s", err.c_str());
    CHECK(macroSideText(side) == "none | +a -a", "text of a release-only side: %s", macroSideText(side).c_str());
    CHECK(parseMacroSide("+a | none", &side, &err) && side.release.empty() && macroSideText(side) == "+a", "an explicit none release part is dropped");
    CHECK(!parseMacroSide("none", &side, &err) && err.find("at least one step") != std::string::npos, "a side needs steps: %s", err.c_str());
    CHECK(!parseMacroSide("none | none", &side, &err), "two empty parts");
    CHECK(!parseMacroSide("", &side, &err), "empty side");
    CHECK(!parseMacroSide("+a | -a | +b", &side, &err) && err.find("one '|'") != std::string::npos, "two bars: %s", err.c_str());
    CHECK(!parseMacroSide("+a | -zzz", &side, &err) && err.find("release part") != std::string::npos, "an error in the release part: %s", err.c_str());
    // each part has its own 256 steps
    CHECK(parseMacroSide(repeatSteps("wait:1", kMaxMacroSteps) + " | " + repeatSteps("wait:1", kMaxMacroSteps), &side, &err), "256 + 256: %s", err.c_str());
    CHECK(!parseMacroSide("+a | " + repeatSteps("wait:1", kMaxMacroSteps + 1), &side, &err), "257 in the release part");

    MacroAction a;
    CHECK(parseMacroAction("+left,+right", &a, &err) && macroActionText(a) == "+left,+right", "two sides: %s", err.c_str());
    CHECK(parseMacroAction("none,+a -a", &a, &err) && a.neg.empty() && a.pos.press.size() == 2 && macroActionText(a) == "none,+a -a", "none on a side");
    CHECK(parseMacroAction("+a -a,none", &a, &err) && a.pos.empty() && macroActionText(a) == "+a -a,none", "none on the other side");
    CHECK(parseMacroAction("+a | -a, +b | -b", &a, &err) && macroActionText(a) == "+a | -a,+b | -b", "release parts on both sides: %s", macroActionText(a).c_str());
    CHECK(!parseMacroAction("none,none", &a, &err), "both sides none");
    CHECK(!parseMacroAction("+a -a", &a, &err) && err.find("two sides") != std::string::npos, "one side only: %s", err.c_str());
    CHECK(!parseMacroAction("+a,+b,+c", &a, &err), "three sides");
    CHECK(!parseMacroAction("+zzz,+a", &a, &err) && err.find("negative side") != std::string::npos, "message names the side: %s", err.c_str());
    CHECK(!parseMacroAction("+a,+zzz", &a, &err) && err.find("positive side") != std::string::npos, "message names the other side: %s", err.c_str());
}

// ---------------------------------------------------------------- the tier B warning text
static void testMacroRisk() {
    MacroSteps v;
    parseMacroSteps("+pageup -pageup +volume_up", &v, nullptr);
    CHECK(macroRiskText(v).empty(), "tier A keys give no warning text: %s", macroRiskText(v).c_str());
    parseMacroSteps("+ctrl +a -a -ctrl", &v, nullptr);
    CHECK(macroRiskText(v) == "ctrl, a", "tier B keys are named: %s", macroRiskText(v).c_str());
    parseMacroSteps("+alt+tab +b +c +d +e", &v, nullptr);
    const std::string r = macroRiskText(v);
    CHECK(r.find("alt+tab") != std::string::npos && r.find("(2 more)") != std::string::npos, "at most three are named: %s", r.c_str());
    parseMacroSteps("-a -b wait:5", &v, nullptr);
    CHECK(macroRiskText(v).empty(), "only key presses are named, a release is not");
}

// ---------------------------------------------------------------- the action parsers
static void testButtonMacro() {
    ButtonActionType t = ButtonActionType::Unbound;
    Key k = Key::Space;
    std::string err;
    MacroSide side;
    CHECK(parseButtonAction("macro:+ctrl +a wait:30 -a -ctrl", &t, &k, &err, nullptr, nullptr, &side) && t == ButtonActionType::Macro &&
              side.press.size() == 5 && side.release.empty(),
          "button macro: %s", err.c_str());
    CHECK(parseButtonAction("  MACRO:+Ctrl +A | -A -Ctrl ", &t, &k, &err, nullptr, nullptr, &side) && t == ButtonActionType::Macro &&
              macroSideText(side) == "+ctrl +a | -a -ctrl",
          "case, blanks and a release part: %s", err.c_str());
    CHECK(parseButtonAction("macro:+a -a", &t, &k, &err) && t == ButtonActionType::Macro, "the optional side output may be left out");
    CHECK(!parseButtonAction("macro:+a,+b", &t, &k, &err) && err.find("without ','") != std::string::npos, "a button macro has no comma: %s", err.c_str());
    CHECK(!parseButtonAction("macro:none", &t, &k, &err), "an empty macro");
    CHECK(!parseButtonAction("macro:", &t, &k, &err), "macro: alone");
    CHECK(!parseButtonAction("macro:+zzz", &t, &k, &err) && err.find("unknown key") != std::string::npos, "unknown key: %s", err.c_str());
    CHECK(!parseButtonAction("macro:+a wait:0", &t, &k, &err), "pause 0");
    CHECK(!parseButtonAction("macro:" + repeatSteps("wait:1", 257), &t, &k, &err), "257 steps");
    CHECK(parseButtonAction("macro:" + repeatSteps("wait:1", 256), &t, &k, &err), "256 steps");
    // an old reader still sees the old grammar: no other action changed
    CHECK(parseButtonAction("key:ctrl+0", &t, &k, &err) && t == ButtonActionType::Key, "key still parses");
    CHECK(!parseButtonAction("macro", &t, &k, &err), "the word alone is no action");
}

static void testChannelMacro() {
    ChannelAction a;
    std::string err;
    CHECK(parseChannelAction("macro:+ctrl +c -c -ctrl,+ctrl +v -v -ctrl", &a, &err) && a.type == ChannelActionType::Macro && a.macro, "motion macro: %s", err.c_str());
    CHECK(!a.usesKeys(), "a macro is not a key action");
    CHECK(channelActionText(a) == "macro:+ctrl +c -c -ctrl,+ctrl +v -v -ctrl", "round trip: %s", channelActionText(a).c_str());
    ChannelAction b;
    CHECK(parseChannelAction(channelActionText(a), &b, &err) && a == b, "text parses back to the same action");
    CHECK(parseChannelAction("macro:none,+a | -a", &a, &err) && a.macro->neg.empty() && a.macro->pos.release.size() == 1, "none side and release part: %s", err.c_str());
    CHECK(channelActionText(a) == "macro:none,+a | -a", "round trip: %s", channelActionText(a).c_str());
    CHECK(parseChannelAction("MACRO: +A -A , none ", &a, &err) && channelActionText(a) == "macro:+a -a,none", "case and blanks: %s", err.c_str());
    ChannelAction c, d;
    parseChannelAction("macro:+a,+b", &c, nullptr);
    parseChannelAction("macro:+a,+c", &d, nullptr);
    CHECK(!(c == d) && c == c, "equality compares the steps");
    CHECK(!(c == ChannelAction()), "a macro differs from the default action");
    CHECK(!parseChannelAction("macro:none,none", &a, &err), "both sides none");
    CHECK(!parseChannelAction("macro:+a -a", &a, &err) && err.find("two sides") != std::string::npos, "one side: %s", err.c_str());
    CHECK(!parseChannelAction("macro:+zzz,none", &a, &err), "unknown key");
    CHECK(!parseChannelAction("macro:" + repeatSteps("wait:1", 257) + ",none", &a, &err) && err.find("too many") != std::string::npos, "257 steps on a side: %s", err.c_str());
    CHECK(parseChannelAction("macro:" + repeatSteps("wait:1", 256) + "," + repeatSteps("wait:1", 256), &a, &err), "256 on each side: %s", err.c_str());
    // the lift takes the digital macro (the trigger is digital)
    CHECK(parseLiftAction("macro:+a -a,none", &a, &err) && a.type == ChannelActionType::Macro, "lift: %s", err.c_str());
    // a macro never makes a profile "all default"
    ModeDef m;
    m.v = a;
    CHECK(!m.allDefaultChannels(), "a macro is a custom channel");
}

// ---------------------------------------------------------------- settings and mode files
static void testMacroInSettings() {
    Settings s;
    std::string err;
    CHECK(applyKey(s, "BUTTON_5", "macro:+ctrl   +a  -a -ctrl", &err), "BUTTON_5: %s", err.c_str());
    CHECK(s.buttons.at(5) == "macro:+ctrl +a -a -ctrl", "the stored text is canonical: %s", s.buttons.at(5).c_str());
    CHECK(applyKey(s, "BUTTON_6", "macro:+a|-a", &err) && s.buttons.at(6) == "macro:+a | -a", "release part: %s", s.buttons.at(6).c_str());
    CHECK(!applyKey(s, "BUTTON_7", "macro:+zzz", &err) && s.buttons.count(7) == 0, "an invalid macro changes nothing");
    CHECK(applyKey(s, "H_TILT_ACTION", "macro:+pageup -pageup,+pagedown -pagedown", &err), "H_TILT_ACTION: %s", err.c_str());
    CHECK(applyKey(s, "LIFT_ACTION", "macro:none,+a -a", &err), "LIFT_ACTION: %s", err.c_str());
    CHECK(!applyKey(s, "V_ACTION", "macro:+a", &err), "a motion macro needs two sides");
    // allKeys writes the canonical text and it reads back to the same settings
    const std::vector<KV> kv = allKeys(s);
    bool sawButton = false, sawAction = false;
    for (const KV &p : kv) {
        if (p.first == "BUTTON_5") sawButton = p.second == "macro:+ctrl +a -a -ctrl";
        if (p.first == "H_TILT_ACTION") sawAction = p.second == "macro:+pageup -pageup,+pagedown -pagedown";
    }
    CHECK(sawButton && sawAction, "allKeys writes the macros");
    Settings back;
    applyKvList(back, kv, kLayerGlobal);
    CHECK(sameSettings(s, back), "the settings read back the same");
    CHECK(allKeys(back) == kv, "and write the same text again");
    // the app layer: BUTTON_n of a program wins over the mode
    Settings app;
    CHECK(applyKeyLayer(app, "BUTTON_2", "macro:+a", kLayerApp, &err) && app.app_buttons.count(2) == 1, "macro in an [app:] layer: %s", err.c_str());
}

static void testMacroInProfileText() {
    const std::string text =
        "[global]\n"
        "BUTTON_5 = macro:+ctrl +a -a -ctrl\n"
        "BUTTON_6 = macro:+pageup -pageup   # a comment\n"
        "H_PUSH_ACTION = macro:+left -left,none\n"
        "BUTTON_7 = macro:+a | -zzz\n";
    ProfileSet ps;
    std::vector<std::string> w;
    CHECK(parseProfilesText(text, "t.conf", &ps, &w), "parse");
    const Settings s = ps.resolve("", "");
    CHECK(s.buttons.count(5) && s.buttons.at(6) == "macro:+pageup -pageup" && s.actions[kLayerGlobal].partSet[PART_H_PUSH], "loaded: %s", s.buttons.count(6) ? s.buttons.at(6).c_str() : "-");
    CHECK(s.buttons.count(7) == 0, "the invalid macro line was dropped");
    bool warnedInvalid = false, warnedTierB = false, warnedTierA = false;
    for (const std::string &m : w) {
        if (m.find("BUTTON_7") != std::string::npos) warnedInvalid = true;
        if (m.find("BUTTON_5") != std::string::npos && m.find("macro") != std::string::npos && m.find("loaded anyway") != std::string::npos) warnedTierB = true;
        if (m.find("BUTTON_6") != std::string::npos || m.find("H_PUSH_ACTION") != std::string::npos) warnedTierA = true;
    }
    CHECK(warnedInvalid, "the invalid line is reported");
    CHECK(warnedTierB, "tier B keys give the plain load warning");
    CHECK(!warnedTierA, "tier A keys give none");
    // an old reader: a line it does not know is dropped with the existing warning, nothing else changes
    ProfileSet ps2;
    CHECK(parseProfilesText("[global]\nZOOM_SENS = 7\n", "t2.conf", &ps2, nullptr) && ps2.resolve("", "").zoom.sensitivity == 7, "files without macros are unaffected");
}

static void testMacroInModeText() {
    // a joystick mode file: a macro on a button and on a motion, loaded from a directory like the daemon does
    const std::string dir = tu::makeTempDir("macrotest");
    {
        std::ofstream f(dir + "/macro.conf", std::ios::binary | std::ios::trunc);
        f << "BUTTON_3 = macro:+alt+tab wait:50 -alt+tab | +shift -shift\n"
             "H_ACTION = macro:+left -left,+right -right\n"
             "BUTTON_4 = macro:+zzz\n"
             "BUTTON_5 = macro:+pageup -pageup\n";
    }
    std::vector<std::string> w;
    const ModeSet set = loadModes(dir, &w);
    const ModeDef *m = set.find("macro");
    CHECK(m != nullptr, "the mode file loaded");
    if (m) {
        CHECK(m->h.type == ChannelActionType::Macro && m->h.macro && m->h.macro->pos.press.size() == 2, "the motion macro");
        CHECK(!m->allDefaultChannels(), "a mode with a macro runs the custom path");
        const ButtonBinding *b3 = nullptr, *b4 = nullptr;
        for (const ButtonBinding &b : m->buttons) {
            if (b.button == 3) b3 = &b;
            if (b.button == 4) b4 = &b;
        }
        CHECK(b3 && b3->type == ButtonActionType::Macro && b3->macro && b3->macro->press.size() == 3 && b3->macro->release.size() == 2,
              "the button macro with a release part");
        CHECK(b4 == nullptr, "the invalid macro line was dropped");
    }
    bool warnedInvalid = false, warnedRisk = false;
    for (const std::string &x : w) {
        if (x.find("BUTTON_4") != std::string::npos) warnedInvalid = true;
        if (x.find("mode macro can press shortcut chords") != std::string::npos && x.find("BUTTON_3") != std::string::npos) warnedRisk = true;
    }
    CHECK(warnedInvalid, "the invalid line is reported");
    CHECK(warnedRisk, "a tier B macro gives the plain warning of the mode loader");
    // the long lines of a macro fit (the old mode line limit was 4096 bytes)
    {
        std::ofstream f(dir + "/macro.conf", std::ios::binary | std::ios::trunc);
        f << "BUTTON_1 = macro:" << repeatSteps("+a -a", 128) << "\n";
    }
    w.clear();
    const ModeSet set2 = loadModes(dir, &w);
    const ModeDef *m2 = set2.find("macro");
    CHECK(m2 && m2->buttons.size() == 1 && m2->buttons[0].macro && m2->buttons[0].macro->press.size() == 256, "a 256-step macro line loads: %s", w.empty() ? "" : w[0].c_str());
    tu::removeAll(dir);
}

int main() {
    testMacroSteps();
    testMacroStepLists();
    testMacroSides();
    testMacroRisk();
    testButtonMacro();
    testChannelMacro();
    testMacroInSettings();
    testMacroInProfileText();
    testMacroInModeText();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_macro OK");
    return 0;
}
