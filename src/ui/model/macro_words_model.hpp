// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The words around a macro (stage 1 of docs/plan-macro-engine.md), toolkit-free: the steps in a few words for chips and
// summaries ("Ctrl+C, 30 ms, ..."), what the user needs to know about the run (further triggers are ignored, a release part is
// dropped while the press part runs) and the check of a typed pause. The steps, the grammar and the text are the ones of the macro core
// (src/macro.hpp); this file only words them. All texts are English source strings that the view translates.
//
//   macroStepsWords(steps, strings)        // "Ctrl+C, 30 ms, Ctrl+V"; a press and release of one chord is one word
//   macroSideWords(side, strings)          // "Ctrl+C, 30 ms, ... | Ctrl+V" (press part | release part)
// The lines of the dialog that need the draft (macroWhatYouGet, macroSimplerHint) are in macro_dialog_words_model.hpp.
#pragma once
#include <string>
#include <vector>

#include "macro.hpp"
#include "ui/model/key_tree_model.hpp"
#include "ui/model/macro_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

namespace macro_words_detail {
struct Tok {
    enum class K { Tap, Down, Up, Wait } k = K::Wait;
    std::string chord;  // Tap, Down, Up: "ctrl+c"
    int ms = 0;         // Wait
};

// The steps as words: a Down directly followed by the Up of the same chord is a tap, and a held modifier around one tap is part
// of it ("+ctrl +c -c -ctrl" is the tap "ctrl+c"). Whatever does not collapse stays a step of its own.
inline std::vector<Tok> tokens(const MacroSteps &steps) {
    std::vector<Tok> t;
    for (size_t i = 0; i < steps.size(); ++i) {
        const MacroStep &s = steps[i];
        Tok k;
        if (s.kind == MacroStep::Kind::Wait) {
            k.k = Tok::K::Wait;
            k.ms = s.waitMs;
        } else if (s.kind == MacroStep::Kind::Down && i + 1 < steps.size() && steps[i + 1].kind == MacroStep::Kind::Up &&
                   macroStepChord(steps[i + 1]) == macroStepChord(s)) {
            k.k = Tok::K::Tap;
            k.chord = macroStepChord(s);
            ++i;
        } else {
            k.k = s.kind == MacroStep::Kind::Down ? Tok::K::Down : Tok::K::Up;
            k.chord = macroStepChord(s);
        }
        t.push_back(k);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i + 2 < t.size(); ++i) {
            if (t[i].k == Tok::K::Down && t[i + 1].k == Tok::K::Tap && t[i + 2].k == Tok::K::Up && t[i].chord == t[i + 2].chord) {
                Tok k;
                k.k = Tok::K::Tap;
                k.chord = t[i].chord + "+" + t[i + 1].chord;
                t.erase(t.begin() + long(i), t.begin() + long(i) + 3);
                t.insert(t.begin() + long(i), k);
                changed = true;
                break;
            }
        }
    }
    return t;
}

// "ctrl+c" -> "Ctrl+C", in the German interface the names of the keys of the German keyboard (the English labels of the key tree).
inline std::string chordWords(const std::string &chord, const StringProvider &s) {
    return chordDisplay(chord, s, [](const std::string &part) { return keyTreeLabel(part); });
}
}  // namespace macro_words_detail

// The steps in words, at most `maxWords` of them, then ", ...". "" for no steps.
inline std::string macroStepsWords(const MacroSteps &steps, const StringProvider &s, size_t maxWords = 4) {
    using K = macro_words_detail::Tok::K;
    const std::vector<macro_words_detail::Tok> toks = macro_words_detail::tokens(steps);
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i == maxWords) {
            out += ", ...";
            break;
        }
        const macro_words_detail::Tok &t = toks[i];
        std::string w;
        switch (t.k) {
            case K::Tap: w = macro_words_detail::chordWords(t.chord, s); break;
            case K::Down: w = format(s.tr("Press %1"), {macro_words_detail::chordWords(t.chord, s)}); break;
            case K::Up: w = format(s.tr("Release %1"), {macro_words_detail::chordWords(t.chord, s)}); break;
            case K::Wait: w = format(s.tr("%1 ms"), {std::to_string(t.ms)}); break;
        }
        if (i) out += ", ";
        out += w;
    }
    return out;
}

// One side: the press part, and after " | " the release part when there is one.
inline std::string macroSideWords(const MacroSide &side, const StringProvider &s, size_t maxWords = 4) {
    std::string out = macroStepsWords(side.press, s, maxWords);
    if (!side.release.empty()) out += (out.empty() ? "" : " | ") + format(s.tr("on release: %1"), {macroStepsWords(side.release, s, maxWords)});
    return out;
}

// A motion's macro for the chips: "Negative: ...; positive: ...", a direction without steps is left out.
inline std::string macroActionWords(const MacroAction &a, const StringProvider &s, size_t maxWords = 3) {
    const bool neg = !a.neg.empty(), pos = !a.pos.empty();
    if (neg && pos) return format(s.tr("negative: %1; positive: %2"), {macroSideWords(a.neg, s, maxWords), macroSideWords(a.pos, s, maxWords)});
    if (neg) return format(s.tr("negative: %1"), {macroSideWords(a.neg, s, maxWords)});
    if (pos) return format(s.tr("positive: %1"), {macroSideWords(a.pos, s, maxWords)});
    return std::string();
}

// "4 steps", "1 step", "none".
inline std::string macroCountWords(size_t n, const StringProvider &s) {
    if (n == 0) return s.tr("none");
    if (n == 1) return s.tr("1 step");
    return format(s.tr("%1 steps"), {std::to_string(n)});
}

// What the user needs to know about a run. Two sentences; the view shows them under the macro editor.
inline std::string macroRunNotes(const StringProvider &s) {
    return s.tr("While a macro runs, further triggers are ignored until it is done. A release part runs only when the press part is finished by then: if the press part still runs when you let go, the release part is dropped.");
}

// A typed pause (milliseconds). false + *err (English source text) when it is not a whole number in the range of the core.
inline bool checkPauseText(const std::string &text, std::string *err) {
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
    const std::string v = text.substr(a, b - a);
    auto fail = [&](const char *why) {
        if (err) *err = why;
        return false;
    };
    if (v.empty()) return fail("the pause is empty: enter a number of milliseconds");
    for (char c : v)
        if (c < '0' || c > '9') return fail("the pause must be a whole number of milliseconds");
    if (v.size() > 6) return fail("the pause must be between 1 and 60000 milliseconds");
    const long n = std::stol(v);
    if (n < kMinMacroWaitMs || n > kMaxMacroWaitMs) return fail("the pause must be between 1 and 60000 milliseconds");
    return true;
}

}  // namespace spn::ui
