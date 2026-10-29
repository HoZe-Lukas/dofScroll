// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The step list of a macro behind the recording dialog (macro_model.hpp): steps and their text, editing (change, delete,
// reorder, the limit of 256 steps), reading a stored side back, and the recorder: measured pauses or the default of 30 ms,
// no repeat, no release of a key that was not pressed while recording, the end by the step limit and the Escape way out.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/macro_model.hpp"

using namespace spn;
using namespace spn::ui;

int main() {
    // ---- steps and their text ----
    assert(macroStepText(macroDown("ctrl+c")) == "+ctrl+c" && macroStepText(macroUp("c")) == "-c" && macroStepText(MacroStep::wait(30)) == "wait:30");
    assert(macroStepValid(macroDown("ctrl+c")) && !macroStepValid(macroDown("ctrl+")) && !macroStepValid(macroDown("nosuchkey")));
    assert(!macroStepValid(macroDown("c+ctrl")));  // modifiers first, then the key
    assert(macroStepValid(macroDown("shift")) && macroStepValid(macroDown("ctrl+shift")));  // a lone modifier is a key
    assert(macroStepValid(MacroStep::wait(1)) && macroStepValid(MacroStep::wait(60000)) && !macroStepValid(MacroStep::wait(0)) && !macroStepValid(MacroStep::wait(60001)));
    MacroStep p;
    assert(parseMacroStep("+ctrl+c", &p, nullptr) && p == macroDown("ctrl+c"));
    assert(parseMacroStep("-a", &p, nullptr) && p == macroUp("a"));
    assert(parseMacroStep("wait:250", &p, nullptr) && p == MacroStep::wait(250));
    for (const char *bad : {"", "+", "-", "a", "wait:", "wait:0", "wait:61000", "wait:1x", "+nosuchkey", "wait:123456"}) assert(!parseMacroStep(bad, &p, nullptr));

    // ---- the list: add, change, delete, reorder ----
    MacroList s;
    assert(s.empty() && s.text().empty());
    assert(s.add(macroDown("ctrl")) && s.add(macroDown("a")) && s.add(macroUp("a")) && s.add(macroUp("ctrl")));
    assert(s.text() == "+ctrl +a -a -ctrl");
    assert(!s.add(MacroStep::wait(0)) && !s.add(macroDown("nosuchkey")) && s.size() == 4);  // an invalid step is never added
    assert(s.insert(2, MacroStep::wait(30)) && s.text() == "+ctrl +a wait:30 -a -ctrl");
    assert(s.set(2, MacroStep::wait(45)) && s.text() == "+ctrl +a wait:45 -a -ctrl");
    assert(!s.set(2, MacroStep::wait(0)) && !s.set(9, MacroStep::wait(5)) && s.text() == "+ctrl +a wait:45 -a -ctrl");
    assert(s.moveDown(1) && s.text() == "+ctrl wait:45 +a -a -ctrl");
    assert(s.moveUp(2) && s.text() == "+ctrl +a wait:45 -a -ctrl");
    assert(!s.moveUp(0) && !s.moveDown(4) && s.text() == "+ctrl +a wait:45 -a -ctrl");
    assert(s.remove(2) && s.text() == "+ctrl +a -a -ctrl" && !s.remove(9));
    s.clear();
    assert(s.empty());

    // ---- the limit of 256 steps (every step counts, pauses too) ----
    for (size_t i = 0; i < kMaxMacroSteps; ++i) assert(s.add(i % 2 ? MacroStep::wait(5) : macroDown("a")));
    assert(s.size() == 256 && s.full() && !s.add(MacroStep::wait(5)) && !s.insert(0, MacroStep::wait(5)));
    assert(s.remove(0) && !s.full() && s.add(MacroStep::wait(5)));

    // ---- reading a stored side ----
    std::string err;
    MacroList r;
    assert(r.parse("+ctrl +a wait:30 -a -ctrl", &err) && r.size() == 5 && r.text() == "+ctrl +a wait:30 -a -ctrl");
    assert(r.parse("", &err) && r.empty());
    assert(!r.parse("+a oops", &err) && err.find("oops") != std::string::npos);
    std::string many;
    for (size_t i = 0; i < 257; ++i) many += i ? " wait:5" : "wait:5";
    assert(!r.parse(many, &err) && err.find("256") != std::string::npos);

    // ---- the recorder with the measured times ----
    MacroRecorder rec(true);
    assert(rec.press("a", 0) == MacroRecorder::Ignored);  // not recording: nothing is kept
    rec.start();
    assert(rec.recording());
    assert(rec.press("ctrl", 1000) == MacroRecorder::Recorded);  // the first step has no pause before it
    assert(rec.press("c", 1100) == MacroRecorder::Recorded);
    assert(rec.press("c", 1130) == MacroRecorder::Ignored);      // the keyboard repeat
    assert(rec.release("c", 1180) == MacroRecorder::Recorded);
    assert(rec.release("ctrl", 1180) == MacroRecorder::Recorded);  // no gap: no pause step
    assert(rec.release("x", 1200) == MacroRecorder::Ignored);      // was not pressed while recording
    assert(rec.steps().text() == "+ctrl wait:100 +c wait:80 -c -ctrl");
    rec.stop();
    assert(!rec.recording() && rec.press("a", 2000) == MacroRecorder::Ignored && rec.steps().text() == "+ctrl wait:100 +c wait:80 -c -ctrl");

    // ---- without the times: the default pause between the steps ----
    MacroRecorder fixed(false);
    fixed.start();
    fixed.press("a", 5);
    fixed.release("a", 9000);
    fixed.press("b", 9001);
    assert(fixed.steps().text() == "+a wait:30 -a wait:30 +b");
    // a pause longer than 60 s is cut to 60 s
    MacroRecorder slow(true);
    slow.start();
    slow.press("a", 0);
    slow.release("a", 120000);
    assert(slow.steps().text() == "+a wait:60000 -a");

    // ---- recording appends to what is there ----
    MacroList have;
    have.add(macroDown("shift"));
    MacroRecorder more(false);
    more.start(have);
    more.press("a", 0);
    assert(more.steps().text() == "+shift +a");  // the first new step follows directly; the user can add a pause by hand

    // ---- the 256th step ends the recording ----
    MacroRecorder full(true);
    full.start();
    MacroRecorder::Result last = MacroRecorder::Recorded;
    for (int i = 0; i < 200 && last == MacroRecorder::Recorded; ++i) {
        last = full.press("a", i * 10);
        if (last == MacroRecorder::Recorded) last = full.release("a", i * 10 + 5);
    }
    assert(last == MacroRecorder::Full && full.full() && !full.recording() && full.steps().size() <= kMaxMacroSteps);

    // ---- Escape three times in a row is the way out for the keyboard, and is not part of the macro ----
    MacroRecorder esc(true);
    esc.start();
    esc.press("a", 0);
    esc.release("a", 10);
    esc.press("esc", 100);
    esc.release("esc", 110);
    esc.press("esc", 200);
    esc.release("esc", 210);
    assert(esc.recording());
    esc.press("esc", 300);
    assert(!esc.recording() && esc.endedByEscape() && esc.steps().text() == "+a wait:10 -a");
    // another key in between starts the count again
    MacroRecorder two(true);
    two.start();
    two.press("esc", 0);
    two.release("esc", 5);
    two.press("a", 10);
    two.release("a", 15);
    two.press("esc", 20);
    two.release("esc", 25);
    two.press("esc", 30);
    assert(two.recording() && !two.endedByEscape());

    std::puts("test_ui_macro: ok");
    return 0;
}
