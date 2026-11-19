// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// One vocabulary for the stored action texts of a motion that more than one surface can write (stage 1 of
// docs/plan-macro-engine.md), toolkit-free. Before, `keys:` was "Other keys" on the tile, "Keys (one key per direction)" in
// the list of a profile and "Keys (a rate of key presses)" in the joystick mode editor, and `step:` was "One key tap per push"
// and "Step (one key press per push)". Now every surface takes the label from here, and the table in docs/ui.md lists the same
// rows (tests/test_ui_macro_words.cpp keeps the two together).
//
//   actionVocabulary(strings)       // the rows: stored form, label, words
//   vocabularyLabel(Vocab::Keys, s) // "Keys that repeat while pushed"
#pragma once
#include <string>
#include <vector>

#include "ui/model/strings.hpp"

namespace spn::ui {

enum class Vocab { Keys, Step, Click, Hold, Macro };

struct VocabularyRow {
    Vocab id = Vocab::Keys;
    std::string stored;  // the form in the file (never translated); the mouse clicks are step: with click, rightclick or middleclick
    std::string label;   // the label every surface shows
    std::string words;   // what it does, in a sentence
};

inline std::string vocabularyLabel(Vocab v, const StringProvider &s) {
    switch (v) {
        case Vocab::Keys: return s.tr("Keys that repeat while pushed");
        case Vocab::Step: return s.tr("One key tap per push");
        case Vocab::Click: return s.tr("Mouse click per push");
        case Vocab::Hold: return s.tr("Hold a key or button while pushed");
        case Vocab::Macro: return s.tr("Macro");
    }
    return std::string();
}

inline std::vector<VocabularyRow> actionVocabulary(const StringProvider &s) {
    return {{Vocab::Keys, "keys:<neg>,<pos>", vocabularyLabel(Vocab::Keys, s), s.tr("A key or combination per direction; the further you push, the faster it repeats.")},
            {Vocab::Step, "step:<neg>,<pos>", vocabularyLabel(Vocab::Step, s), s.tr("A key or combination per direction, pressed once each time you push past the trigger level.")},
            {Vocab::Click, "step:<neg>,<pos>", vocabularyLabel(Vocab::Click, s), s.tr("A mouse click per direction, once each time you push past the trigger level.")},
            {Vocab::Hold, "hold:<neg>,<pos>", vocabularyLabel(Vocab::Hold, s), s.tr("A key, combination or mouse button is held while you push past the trigger level and let go when you ease back.")},
            {Vocab::Macro, "macro:<press> | <release>", vocabularyLabel(Vocab::Macro, s), s.tr("Key presses and pauses that run one after the other, once per trigger; the part after | runs when you let go.")}};
}

}  // namespace spn::ui
