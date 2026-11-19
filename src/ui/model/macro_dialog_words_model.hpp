// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The two lines of the function dialog that need the draft of a macro (stage 1 of docs/plan-macro-engine.md), toolkit-free: the
// one-line "what you get" under the Action field (the stored form named in words) and the hint where a simpler kind does the
// same. The words of the steps are in macro_words_model.hpp.
//
//   macroWhatYouGet(target, draft, str)    // "Stored as a macro: press part 4 steps, release part none. ..."
//   macroSimplerHint(target, draft, str)   // "" or "A single key or combination does not need a macro: ..."
#pragma once
#include <string>
#include <vector>

#include "ui/model/macro_words_model.hpp"
#include "ui/model/new_function_model.hpp"

namespace spn::ui {

// The side of the draft as the daemon will read it.
inline MacroSide macroDraftSide(const FunctionDraft &d, bool pos) {
    MacroSide side;
    side.press = (pos ? d.macroPos : d.macroNeg).steps();
    side.release = (pos ? d.macroPosRelease : d.macroNegRelease).steps();
    return side;
}

// The one line under the Action field: the stored form named in words, and when each part runs.
inline std::string macroWhatYouGet(const FunctionTarget &t, const FunctionDraft &d, const StringProvider &s) {
    const MacroSide neg = macroDraftSide(d, false), pos = macroDraftSide(d, true);
    if (!t.isMotion()) {
        if (neg.empty()) return s.tr("Stored as a macro, but it has no steps yet.");
        return format(s.tr("Stored as a macro: press part %1, release part %2. The press part runs when you press the button, the release part when you let go."),
                      {macroCountWords(neg.press.size(), s), macroCountWords(neg.release.size(), s)});
    }
    if (neg.empty() && pos.empty()) return s.tr("Stored as a macro, but it has no steps yet.");
    return format(s.tr("Stored as a macro, one per direction: negative direction: press part %1, release part %2; positive direction: press part %3, release part %4. The press part runs when you push past the trigger level, the release part when you ease back to half of it."),
                  {macroCountWords(neg.press.size(), s), macroCountWords(neg.release.size(), s), macroCountWords(pos.press.size(), s),
                   macroCountWords(pos.release.size(), s)});
}

namespace macro_words_detail {
// True when the part is exactly one key or combination pressed and let go (a macro that "Key or combination" does too).
inline bool singleTap(const MacroSteps &steps) {
    const std::vector<Tok> t = tokens(steps);
    return t.size() == 1 && t[0].k == Tok::K::Tap;
}
}  // namespace macro_words_detail

// A hint where a simpler kind does the same: every part that has steps is one key or combination, and there is no release
// part. "" otherwise.
inline std::string macroSimplerHint(const FunctionTarget &t, const FunctionDraft &d, const StringProvider &s) {
    bool any = false;
    for (int pos = 0; pos < (t.isMotion() ? 2 : 1); ++pos) {
        const MacroSide side = macroDraftSide(d, pos == 1);
        if (side.empty()) continue;
        if (!side.release.empty() || !macro_words_detail::singleTap(side.press)) return std::string();
        any = true;
    }
    if (!any) return std::string();
    return t.isMotion() ? s.tr("A single key or combination per direction does not need a macro: choose Key or combination (one key tap per push) instead.")
                        : s.tr("A single key or combination does not need a macro: choose Key or combination here, or Press key in the list of the button.");
}

}  // namespace spn::ui
