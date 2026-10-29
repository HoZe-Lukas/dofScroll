// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Macro steps: parser, canonical text and validation. See macro.hpp for the grammar.
#include "macro.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "modes.hpp"
#include "utf8.hpp"

namespace spn {

namespace {

bool fail(std::string *err, const std::string &msg) {
    if (err) *err = msg;
    return false;
}

bool isSep(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Splits at runs of spaces (and tabs): the canonical separator is one space, but a hand-written file may use more.
std::vector<std::string> splitTokens(const std::string &text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSep(text[i])) ++i;
        const size_t b = i;
        while (i < text.size() && !isSep(text[i])) ++i;
        if (i > b) out.push_back(text.substr(b, i - b));
    }
    return out;
}

std::string quoted(const std::string &tok) {
    // Only for messages: control characters and invalid bytes dropped, length capped on a character boundary.
    return "'" + utf8::sanitizeDisplay(tok, 40) + "'";
}

std::string chordText(Key key, const ChordMods &mods) {
    std::string t;
    for (uint8_t i = 0; i < mods.n; ++i) t += std::string(keyName(mods.k[i])) + "+";
    return t + keyName(key);
}

}  // namespace

bool parseMacroStep(const std::string &text, MacroStep *out, std::string *err) {
    const std::string t = utf8::lowerAscii(utf8::trim(text));
    if (t.empty()) return fail(err, "empty step");
    MacroStep s;
    if (t.compare(0, 5, "wait:") == 0) {
        const std::string n = t.substr(5);
        if (n.empty() || n.size() > 5 || n.find_first_not_of("0123456789") != std::string::npos)
            return fail(err, "wait:<ms> needs a whole number of milliseconds (" + std::to_string(kMinMacroWaitMs) + ".." +
                                 std::to_string(kMaxMacroWaitMs) + ")");
        const int ms = std::atoi(n.c_str());
        if (ms < kMinMacroWaitMs || ms > kMaxMacroWaitMs)
            return fail(err, "a pause must be " + std::to_string(kMinMacroWaitMs) + ".." + std::to_string(kMaxMacroWaitMs) +
                                 " ms, got " + std::to_string(ms));
        s = MacroStep::wait(ms);
    } else if (t[0] == '+' || t[0] == '-') {
        Key key = Key::Space;
        ChordMods mods;
        std::string e;
        if (!parseChord(t.substr(1), &key, &mods, &e, true)) return fail(err, e);
        s = t[0] == '+' ? MacroStep::down(key, mods) : MacroStep::up(key, mods);
    } else {
        return fail(err, "a step is +<key> (press), -<key> (release) or wait:<ms>, got " + quoted(t));
    }
    if (out) *out = s;
    return true;
}

std::string macroStepText(const MacroStep &step) {
    switch (step.kind) {
        case MacroStep::Kind::Down: return "+" + chordText(step.key, step.mods);
        case MacroStep::Kind::Up: return "-" + chordText(step.key, step.mods);
        case MacroStep::Kind::Wait: return "wait:" + std::to_string(step.waitMs);
    }
    return std::string();
}

size_t macroStepKeys(const MacroStep &step, Key *out) {
    if (step.kind == MacroStep::Kind::Wait) return 0;
    size_t n = 0;
    for (uint8_t i = 0; i < step.mods.n; ++i) out[n++] = step.mods.k[i];
    out[n++] = step.key;
    return n;
}

std::string macroStepsProblem(const MacroSteps &steps) {
    if (steps.size() > kMaxMacroSteps)
        return "too many steps (" + std::to_string(steps.size()) + ", max. " + std::to_string(kMaxMacroSteps) + ")";
    for (size_t i = 0; i < steps.size(); ++i)
        if (steps[i].kind == MacroStep::Kind::Wait && (steps[i].waitMs < kMinMacroWaitMs || steps[i].waitMs > kMaxMacroWaitMs))
            return "step " + std::to_string(i + 1) + ": a pause must be " + std::to_string(kMinMacroWaitMs) + ".." +
                   std::to_string(kMaxMacroWaitMs) + " ms, got " + std::to_string(steps[i].waitMs);
    return std::string();
}

bool parseMacroSteps(const std::string &text, MacroSteps *out, std::string *err) {
    const std::string t = utf8::lowerAscii(utf8::trim(text));
    MacroSteps steps;
    if (!t.empty() && t != "none") {
        const std::vector<std::string> toks = splitTokens(t);
        if (toks.size() > kMaxMacroSteps)
            return fail(err, "too many steps (" + std::to_string(toks.size()) + ", max. " + std::to_string(kMaxMacroSteps) + ")");
        for (size_t i = 0; i < toks.size(); ++i) {
            MacroStep s;
            std::string e;
            if (!parseMacroStep(toks[i], &s, &e)) return fail(err, "step " + std::to_string(i + 1) + " (" + quoted(toks[i]) + "): " + e);
            steps.push_back(s);
        }
    }
    if (out) *out = std::move(steps);
    return true;
}

std::string macroStepsText(const MacroSteps &steps) {
    if (steps.empty()) return "none";
    std::string t;
    for (const MacroStep &s : steps) t += (t.empty() ? "" : " ") + macroStepText(s);
    return t;
}

bool parseMacroSide(const std::string &text, MacroSide *out, std::string *err) {
    const size_t bar = text.find('|');
    if (bar != std::string::npos && text.find('|', bar + 1) != std::string::npos)
        return fail(err, "a macro side has at most one '|' (press part | release part)");
    MacroSide side;
    std::string e;
    if (!parseMacroSteps(bar == std::string::npos ? text : text.substr(0, bar), &side.press, &e)) return fail(err, e);
    if (bar != std::string::npos && !parseMacroSteps(text.substr(bar + 1), &side.release, &e))
        return fail(err, "release part: " + e);
    if (side.empty()) return fail(err, "a macro needs at least one step");
    if (out) *out = std::move(side);
    return true;
}

std::string macroSideText(const MacroSide &side) {
    if (side.empty()) return "none";
    std::string t = macroStepsText(side.press);
    if (!side.release.empty()) t += " | " + macroStepsText(side.release);
    return t;
}

bool parseMacroAction(const std::string &text, MacroAction *out, std::string *err) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos)
        return fail(err, "macro:<neg>,<pos> expects exactly two sides (a side may be none)");
    MacroAction a;
    std::string e;
    const std::string negText = text.substr(0, comma), posText = text.substr(comma + 1);
    const bool negNone = utf8::lowerAscii(utf8::trim(negText)) == "none", posNone = utf8::lowerAscii(utf8::trim(posText)) == "none";
    if (!negNone && !parseMacroSide(negText, &a.neg, &e)) return fail(err, "negative side: " + e);
    if (!posNone && !parseMacroSide(posText, &a.pos, &e)) return fail(err, "positive side: " + e);
    if (a.neg.empty() && a.pos.empty()) return fail(err, "macro: needs steps on at least one side (use none for the whole action)");
    if (out) *out = std::move(a);
    return true;
}

std::string macroActionText(const MacroAction &action) { return macroSideText(action.neg) + "," + macroSideText(action.pos); }

std::string macroRiskText(const MacroSteps &steps) {
    std::string out;
    size_t named = 0, more = 0;
    for (const MacroStep &s : steps) {
        if (s.kind != MacroStep::Kind::Down || keyAnalogAllowed(s.key, s.mods)) continue;
        if (named >= 3) {
            ++more;
            continue;
        }
        out += (out.empty() ? "" : ", ") + chordText(s.key, s.mods);
        ++named;
    }
    if (more) out += ", ... (" + std::to_string(more) + " more)";
    return out;
}

std::string macroRiskText(const MacroSide &side) {
    const std::string a = macroRiskText(side.press), b = macroRiskText(side.release);
    return a + (a.empty() || b.empty() ? "" : ", ") + b;
}

std::string macroRiskText(const MacroAction &action) {
    const std::string a = macroRiskText(action.neg), b = macroRiskText(action.pos);
    return a + (a.empty() || b.empty() ? "" : ", ") + b;
}

}  // namespace spn
