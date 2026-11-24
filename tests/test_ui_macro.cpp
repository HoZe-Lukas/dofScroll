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

    // ---- recording goes in at the cursor, not only at the end ----
    {
        MacroList base;
        assert(base.parse("+ctrl wait:30 -ctrl", nullptr));
        MacroRecorder mid(false);
        mid.start(base, 1);  // behind the first step
        assert(mid.lastRecorded() == -1);
        mid.press("a", 0);
        assert(mid.steps().text() == "+ctrl +a wait:30 -ctrl" && mid.lastRecorded() == 1);
        mid.release("a", 10);
        assert(mid.steps().text() == "+ctrl +a wait:30 -a wait:30 -ctrl" && mid.lastRecorded() == 3);
        mid.stop();
        // at the start, at the end, past the end (the end), and by default (the end)
        MacroRecorder first(false);
        first.start(base, 0);
        first.press("b", 0);
        assert(first.steps().text() == "+b +ctrl wait:30 -ctrl" && first.lastRecorded() == 0);
        MacroRecorder end(false);
        end.start(base, base.size());
        end.press("b", 0);
        assert(end.steps().text() == "+ctrl wait:30 -ctrl +b" && end.lastRecorded() == 3);
        MacroRecorder past(false);
        past.start(base, 99);
        past.press("b", 0);
        assert(past.steps().text() == "+ctrl wait:30 -ctrl +b");
        MacroRecorder dflt(false);
        dflt.start(base);
        dflt.press("b", 0);
        assert(dflt.steps().text() == "+ctrl wait:30 -ctrl +b");
        // the limit counts the steps around the cursor too
        MacroList nearFull;
        for (size_t i = 0; i < kMaxMacroSteps - 1; ++i) assert(nearFull.add(macroDown("a")));
        MacroRecorder lim(false);
        lim.start(nearFull, 3);
        assert(lim.press("b", 0) == MacroRecorder::Recorded && lim.steps().size() == kMaxMacroSteps);
        assert(lim.release("b", 5) == MacroRecorder::Full && lim.full() && !lim.recording() && lim.steps().size() == kMaxMacroSteps);
        // Escape three times drops only what was recorded, never the steps around the cursor
        MacroList around;
        assert(around.parse("+esc -esc +x", nullptr));
        MacroRecorder esc3(true);
        esc3.start(around, 2);
        esc3.press("a", 0);
        esc3.release("a", 10);
        esc3.press("esc", 100);
        esc3.release("esc", 110);
        esc3.press("esc", 200);
        esc3.release("esc", 210);
        esc3.press("esc", 300);
        assert(!esc3.recording() && esc3.endedByEscape() && esc3.steps().text() == "+esc -esc +a wait:10 -a +x");
    }

    // ---- the other step kinds: tap, mouse, repeat block, pasted text ----
    assert(macroStepText(macroTap("ctrl+c")) == "tap:ctrl+c" && macroStepChord(macroTap("ctrl+c")) == "ctrl+c" && macroStepChord(macroDown("a")) == "a");
    assert(macroStepChord(MacroStep::wait(5)).empty() && macroStepChord(macroMouse(MacroStep::Kind::MouseTap, 1)).empty() && macroStepChord(macroPaste("x")).empty());
    assert(macroStepValid(macroTap("a")) && !macroStepValid(macroTap("nosuchkey")));
    assert(macroStepText(macroMouse(MacroStep::Kind::MouseTap, 3)) == "rightclick" && macroStepText(macroMouse(MacroStep::Kind::MouseDown, 1)) == "+click");
    assert(!macroStepValid(macroMouse(MacroStep::Kind::MouseTap, 4)) && !macroStepValid(macroMouse(MacroStep::Kind::MouseTap, 0)));
    assert(macroStepText(macroRepeat(3)) == "repeat:3" && macroStepText(macroRepeat(0)) == "repeat" && !macroStepValid(macroRepeat(1001)) && !macroStepValid(macroRepeat(-1)));
    assert(macroStepValid(macroPaste("Hello, World")) && !macroStepValid(macroPaste("a#b")) && !macroStepValid(macroPaste("")) && !macroStepValid(MacroStep::paste("a\\b")));
    assert(macroStepText(macroPaste("Hi there")) == "paste:\"Hi there\"");
    {
        MacroList l;
        assert(l.add(macroRepeat(3)) && l.add(macroTap("a")) && l.add(MacroStep::repeatEnd()) && l.add(macroPaste("Ab, c")) && l.add(macroMouse(MacroStep::Kind::MouseTap, 1)));
        assert(l.text() == "repeat:3 tap:a end paste:\"Ab, c\" click" && l.problem().empty());
        assert(!l.add(MacroStep::paste("a\"b")) && l.size() == 5);  // a text the core refuses is never added
        MacroList open;
        assert(open.add(macroRepeat(3)) && open.add(macroTap("a")) && !open.problem().empty());  // the block is not closed: the dialog says so
        MacroList inf;
        assert(inf.add(macroRepeat(0)) && inf.add(macroTap("a")) && inf.add(MacroStep::repeatEnd()));
        assert(!inf.problem().empty() && inf.problem(true).empty());  // until released needs while_held
        std::string perr;
        MacroList back;
        assert(back.parse("repeat tap:down end", &perr, true) && back.text() == "repeat tap:down end" && !back.parse("repeat tap:down end", &perr));
        assert(l.parse("paste:\"Hello  World, 1|2\" tap:ctrl+v", &perr) && l.at(0).text == "Hello  World, 1|2" && l.size() == 2);
    }
    // ---- the recorder writes a quick press and release as one tap (when asked to) ----
    {
        MacroRecorder m(true);
        assert(!m.mergeTaps());
        m.setMergeTaps(true);
        m.start();
        assert(m.press("ctrl", 0) == MacroRecorder::Recorded && m.press("c", 100) == MacroRecorder::Recorded);
        assert(m.release("c", 160) == MacroRecorder::Recorded && m.release("ctrl", 300) == MacroRecorder::Recorded);
        assert(m.steps().text() == "+ctrl wait:100 tap:c wait:140 -ctrl");
        assert(m.press("a", 400) == MacroRecorder::Recorded && m.release("a", 800) == MacroRecorder::Recorded);  // held 400 ms: not a tap
        assert(m.steps().text() == "+ctrl wait:100 tap:c wait:140 -ctrl wait:100 +a wait:400 -a");
        assert(m.press("b", 900) == MacroRecorder::Recorded && m.press("d", 920) == MacroRecorder::Recorded);
        assert(m.release("b", 950) == MacroRecorder::Recorded);  // another key went down in between: no tap
        assert(m.steps().text().find("+b wait:20 +d wait:30 -b") != std::string::npos);
        MacroRecorder noTimes(false);
        noTimes.setMergeTaps(true);
        noTimes.start();
        assert(noTimes.press("a", 0) == MacroRecorder::Recorded && noTimes.release("a", 5000) == MacroRecorder::Recorded && noTimes.steps().text() == "tap:a");  // without times: always a tap
    }

    std::puts("test_ui_macro: ok");
    return 0;
}
