// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The words around a macro (stage 1 of docs/plan-macro-engine.md): the steps as short words for chips ("Ctrl+C, 30 ms, ..."), the
// "what you get" line and the hint under the action text, the notes about the run, the check of a typed pause, the chips and
// summaries that show the first steps, and the one vocabulary of the stored forms (the same rows as the table in docs/ui.md).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "button_modes.hpp"
#include "ui/model/action_preset_model.hpp"
#include "ui/model/action_vocabulary_model.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/macro_dialog_words_model.hpp"
#include "ui/model/macro_words_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_summary_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
MacroSteps steps(const std::string &text) {
    MacroSteps s;
    std::string err;
    const bool ok = parseMacroSteps(text, &s, &err);
    assert(ok);
    (void)ok;
    return s;
}
}  // namespace

int main() {
    IdentityStrings s;

    // ---- the steps in words: a press and release of one chord is one word, a held modifier around one tap is part of it ----
    assert(macroStepsWords(steps("+ctrl +c -c -ctrl"), s) == "Ctrl+C");
    assert(macroStepsWords(steps("+ctrl +c -c -ctrl wait:30 +ctrl +v -v -ctrl"), s) == "Ctrl+C, 30 ms, Ctrl+V");
    assert(macroStepsWords(steps("+a -a"), s) == "A");
    assert(macroStepsWords(steps("+shift"), s) == "Press Shift");          // held on: not a tap
    assert(macroStepsWords(steps("-shift"), s) == "Release Shift");
    assert(macroStepsWords(steps("+ctrl +shift +a -a -shift -ctrl"), s) == "Ctrl+Shift+A");
    assert(macroStepsWords(steps("wait:500"), s) == "500 ms");
    assert(macroStepsWords(MacroSteps(), s).empty());
    // at most four words, then "..."
    assert(macroStepsWords(steps("+a -a wait:30 +b -b wait:30 +c -c"), s) == "A, 30 ms, B, 30 ms, ...");
    assert(macroStepsWords(steps("+a -a wait:30 +b -b wait:30 +c -c"), s, 2) == "A, 30 ms, ...");

    // ---- a side and a motion's macro ----
    MacroSide side;
    assert(parseMacroSide("+ctrl +c -c -ctrl wait:30 +v | +esc -esc", &side, nullptr));
    assert(macroSideWords(side, s) == "Ctrl+C, 30 ms, Press V | on release: Escape");
    MacroAction act;
    assert(parseMacroAction("+a -a,none", &act, nullptr));
    assert(macroActionWords(act, s) == "negative: A");
    assert(parseMacroAction("none,+b -b", &act, nullptr));
    assert(macroActionWords(act, s) == "positive: B");
    assert(parseMacroAction("+a -a,+b -b", &act, nullptr));
    assert(macroActionWords(act, s) == "negative: A; positive: B");
    assert(macroCountWords(0, s) == "none" && macroCountWords(1, s) == "1 step" && macroCountWords(4, s) == "4 steps");

    // ---- chips and summaries show the first steps ----
    assert(buttonActionWords("macro:+ctrl +c -c -ctrl wait:30 +ctrl +v -v -ctrl", s) == "Macro: Ctrl+C, 30 ms, Ctrl+V");
    assert(buttonActionWords("macro:+a -a | +b -b", s) == "Macro: A | on release: B");
    assert(buttonChip(3, "macro:+ctrl +c -c -ctrl", "", s).text == "3 Macro: Ctrl+C");
    {
        ChannelAction a;
        assert(parseChannelAction("macro:+ctrl +c -c -ctrl wait:30 +a -a,none", &a, nullptr));
        const MotionRow r = motion_detail::rowFor("h_tilt", "Horizontal tilt", "", a, false, false, 0, s);
        assert(r.chip == "Macro" && r.action == "Macro, once per push: negative: Ctrl+C, 30 ms, A");
        assert(r.detail == "Macro: negative: Ctrl+C, 30 ms, ...");
    }

    // ---- "what you get" under the Action field, and the hint where a simpler kind does the same ----
    const FunctionTarget btn = FunctionTarget::buttonNumber(3), tilt = FunctionTarget::motion("h_tilt");
    FunctionDraft d = newDraft(btn, FunctionKind::Macro);
    assert(macroWhatYouGet(btn, d, s) == "Stored as a macro, but it has no steps yet.");
    assert(macroSimplerHint(btn, d, s).empty());
    assert(d.macroNeg.parse("+ctrl +c -c -ctrl", nullptr));
    assert(macroWhatYouGet(btn, d, s).find("Stored as a macro: press part 4 steps, release part none.") == 0);
    assert(macroSimplerHint(btn, d, s).find("does not need a macro") != std::string::npos && macroSimplerHint(btn, d, s).find("Press key") != std::string::npos);
    assert(d.macroNegRelease.parse("+a", nullptr));
    assert(macroWhatYouGet(btn, d, s).find("release part 1 step.") != std::string::npos);
    assert(macroSimplerHint(btn, d, s).empty());  // a release part is more than a key
    d.macroNegRelease.clear();
    assert(d.macroNeg.parse("+ctrl +c -c -ctrl wait:30 +v -v", nullptr));
    assert(macroSimplerHint(btn, d, s).empty());  // more than one key: a macro is the right kind
    FunctionDraft m = newDraft(tilt, FunctionKind::Macro);
    assert(m.macroNeg.parse("+left -left", nullptr) && m.macroPos.parse("+right -right", nullptr));
    assert(macroSimplerHint(tilt, m, s).find("per direction") != std::string::npos);
    assert(macroWhatYouGet(tilt, m, s).find("negative direction: press part 2 steps, release part none; positive direction: press part 2 steps") != std::string::npos);
    assert(m.macroPos.parse("+right -right wait:30 +a", nullptr));
    assert(macroSimplerHint(tilt, m, s).empty());
    // the notes about the run: ignored triggers, a dropped release part
    assert(macroRunNotes(s).find("further triggers are ignored") != std::string::npos && macroRunNotes(s).find("release part is dropped") != std::string::npos);

    // ---- a typed pause is checked, with a message ----
    std::string err;
    for (const char *ok : {"1", "30", " 250 ", "60000"}) assert(checkPauseText(ok, &err));
    assert(!checkPauseText("", &err) && err.find("empty") != std::string::npos);
    assert(!checkPauseText("abc", &err) && err.find("whole number") != std::string::npos);
    assert(!checkPauseText("1.5", &err) && !checkPauseText("-3", &err));
    assert(!checkPauseText("0", &err) && err.find("between 1 and 60000") != std::string::npos);
    assert(!checkPauseText("60001", &err) && !checkPauseText("99999999999", &err));

    // ---- the vocabulary: one label per stored form, the same on every surface ----
    {
        const std::vector<VocabularyRow> rows = actionVocabulary(s);
        assert(rows.size() == 5);
        // the list of a motion (profile page) and the tiles of a joystick mode say the same words
        const auto labelOf = [&](Vocab v) { return vocabularyLabel(v, s); };
        bool keys = false, step = false, click = false, hold = false, macro = false;
        for (const MotionActionChoice &c : motionActionChoices()) {
            if (c.type == ChannelActionType::Keys) keys = labelOf(Vocab::Keys) == c.label;
            if (c.type == ChannelActionType::Step && c.key() == int(ChannelActionType::Step)) step = labelOf(Vocab::Step) == c.label;
            if (c.key() == kChoiceMouseClick) click = labelOf(Vocab::Click) == c.label;
            if (c.type == ChannelActionType::Hold) hold = labelOf(Vocab::Hold) == c.label;
            if (c.type == ChannelActionType::Macro) macro = labelOf(Vocab::Macro) + "..." == c.label && c.key() == kChoiceMacro;
        }
        assert(keys && step && click && hold && macro);
        std::string tiles;
        for (const ActionPreset &p : actionPresets(s)) tiles += p.id + "=" + p.label + ";";
        assert(tiles.find("keys=" + labelOf(Vocab::Keys) + ";") != std::string::npos);
        assert(tiles.find("taps=" + labelOf(Vocab::Step) + ";") != std::string::npos);
        assert(tiles.find("click=" + labelOf(Vocab::Click) + ";") != std::string::npos);
        assert(tiles.find("hold=" + labelOf(Vocab::Hold) + ";") != std::string::npos);
        assert(tiles.find("macro=" + labelOf(Vocab::Macro) + "...;") != std::string::npos);
        // the macro of a motion is the entry that shows a macro form
        ChannelForm f;
        f.type = ChannelActionType::Macro;
        assert(choiceIdOf(f) == kChoiceMacro && choiceType(kChoiceMacro) == ChannelActionType::Macro);
        assert(activePreset(Motion::HTilt, f) == "macro");
        // docs/ui.md has a row for every label (the table of the vocabulary)
        std::ifstream in("docs/ui.md");  // ctest runs in the source directory
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string doc = ss.str();
        assert(!doc.empty());
        for (const VocabularyRow &r : rows) assert(doc.find("| " + r.label + " |") != std::string::npos);
        for (const VocabularyRow &r : rows) {
            std::string cell = r.stored;  // a | in a table cell is written \|
            for (size_t at = cell.find('|'); at != std::string::npos; at = cell.find('|', at + 2)) cell.replace(at, 1, "\\|");
            assert(doc.find("`" + cell + "`") != std::string::npos);
        }
    }

    // ---- the other step kinds in words: tap, mouse, repeat block, pasted text, while held ----
    assert(macroStepsWords(steps("tap:ctrl+c"), s, 10) == "Ctrl+C" && macroStepsWords(steps("tap:down"), s, 10) == "Down arrow");
    assert(macroStepsWords(steps("click rightclick middleclick"), s, 10) == "Click left button, Click right button, Click middle button");
    assert(macroStepsWords(steps("+click wait:30 -click"), s, 10) == "Hold left button, 30 ms, Release left button");
    assert(macroStepsWords(steps("repeat:3 tap:a end"), s, 10) == "Repeat 3 times:, A, end of repeat");
    assert(macroStepsWords(steps("paste:\"Hello, World\""), s, 10) == "Paste \"Hello, World\"");
    assert(macroStepsWords(steps("paste:\"0123456789012345678901234\""), s, 10) == "Paste \"01234567890123456789...\"");
    {
        MacroSide heldSide;
        std::string perr;
        assert(parseMacroSide("while_held repeat tap:down wait:50 end | tap:esc", &heldSide, &perr));
        assert(macroSideWords(heldSide, s, 10) == "while held: Repeat until released:, Down arrow, 50 ms, end of repeat | on release: Escape");
    }
    std::puts("test_ui_macro_words: ok");
    return 0;
}
