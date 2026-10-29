// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The macro function (stage 7b of docs/plan-motion-functions.md): a list of steps that runs once per trigger. Toolkit-free
// model API: parse the stored text to a step list, serialize a step list to text, validation messages and the limits. The
// interface (macro dialog, recorder) and the config parser share it, so both agree on what is a valid macro.
//
// Steps (separated by one or more spaces; a space is never part of a step):
//   +<chord>     key DOWN: the modifiers in the order written, then the key; stays held (+ctrl+c, +shift, +a)
//   -<chord>     key UP: releases the key, then the modifiers in reverse order
//   wait:<ms>    pause, kMinMacroWaitMs..kMaxMacroWaitMs milliseconds
// A key name is one of the closed list in keys.hpp. There is no tap step: a key press is "+a -a". Whatever a macro still
// holds is released when it ends (and when it is aborted), so a press-only macro such as "+ctrl" is valid.
//
// Action text (the part after "macro:"):
//   BUTTON_<n> = macro:<side>                 device button: <side> runs when the button is pressed
//   <MOTION>_ACTION = macro:<neg>,<pos>       motion: one <side> per direction, "none" for a direction without macro
//   <side> = <part>                           the macro that runs when the trigger fires
//          | <part> | <part>                  press part | release part: the second runs when the trigger is released
//                                             (button: released; motion: back at half of the trigger)
//   <part> = none | <step> { <step> }         "none" = nothing in that place; a side needs at least one part with steps
// Example (select all, copy, then type "ab" with shift held):
//   macro:+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b
// Digital only. At most kMaxMacroSteps steps per part (wait steps count), and the running queue is the same size.
// Further triggers while a macro runs are ignored until it is done.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chord.hpp"
#include "keys.hpp"

namespace spn {

constexpr size_t kMaxMacroSteps = 256;              // steps per macro part, and the queue of a running macro
constexpr int kMinMacroWaitMs = 1, kMaxMacroWaitMs = 60000;
constexpr int kDefaultMacroWaitMs = 30;             // the pause the recorder puts between steps when times are not recorded
constexpr size_t kMaxMacroKeysPerStep = kMaxChordMods + 1;  // the modifiers of a chord plus its key

struct MacroStep {
    enum class Kind : uint8_t { Down, Up, Wait };
    Kind kind = Kind::Wait;
    Key key = Key::Space;   // Down, Up: the chord's key (the last name of the chord)
    ChordMods mods;         // Down, Up: its modifiers
    int waitMs = 0;         // Wait

    static MacroStep down(Key k, const ChordMods &m = ChordMods()) {
        MacroStep s;
        s.kind = Kind::Down;
        s.key = k;
        s.mods = m;
        return s;
    }
    static MacroStep up(Key k, const ChordMods &m = ChordMods()) {
        MacroStep s = down(k, m);
        s.kind = Kind::Up;
        return s;
    }
    static MacroStep wait(int ms) {
        MacroStep s;
        s.waitMs = ms;
        return s;
    }
    bool operator==(const MacroStep &o) const {
        if (kind != o.kind) return false;
        if (kind == Kind::Wait) return waitMs == o.waitMs;
        return key == o.key && mods.n == o.mods.n && mods == o.mods;
    }
    bool operator!=(const MacroStep &o) const { return !(*this == o); }
};
using MacroSteps = std::vector<MacroStep>;

// What one trigger runs: the macro of the press (or of the digital trigger) and, optional, the macro of the release.
struct MacroSide {
    MacroSteps press, release;
    bool empty() const { return press.empty() && release.empty(); }
    bool operator==(const MacroSide &o) const { return press == o.press && release == o.release; }
};
// The macro of a motion: one side per direction.
struct MacroAction {
    MacroSide neg, pos;
    bool operator==(const MacroAction &o) const { return neg == o.neg && pos == o.pos; }
};

// ---- one step -------------------------------------------------------------------------------------------------------
// Parses "+ctrl+c", "-a" or "wait:30" (ASCII case folded). false + *err (a message for the user) otherwise.
bool parseMacroStep(const std::string &text, MacroStep *out, std::string *err);
// Canonical text of a step: "+ctrl+c", "-a", "wait:30". The inverse of parseMacroStep.
std::string macroStepText(const MacroStep &step);
// The keys a Down or Up step presses, in the order written (the modifiers, then the key). Returns the count (0 for a Wait).
// out must hold kMaxMacroKeysPerStep entries.
size_t macroStepKeys(const MacroStep &step, Key *out);

// ---- a list of steps ------------------------------------------------------------------------------------------------
// Parses steps separated by spaces. "" and "none" give an empty list. Checks the grammar of every step and the limits
// (macroStepsProblem). false + *err names the step ("step 3 ('+foo'): unknown key 'foo'").
bool parseMacroSteps(const std::string &text, MacroSteps *out, std::string *err);
// Canonical text: the steps separated by one space; an empty list gives "none".
std::string macroStepsText(const MacroSteps &steps);
// Validation of a list built in memory (the dialog): "" if it is valid, else the message: more than kMaxMacroSteps steps,
// a pause outside kMinMacroWaitMs..kMaxMacroWaitMs. An empty list is valid here (a side decides whether it needs steps).
std::string macroStepsProblem(const MacroSteps &steps);

// ---- a side and a motion action -------------------------------------------------------------------------------------
// "<part>" or "<part> | <part>" (see above); at least one part must have steps. Spaces around '|' are optional.
bool parseMacroSide(const std::string &text, MacroSide *out, std::string *err);
// Canonical: "<press>" or "<press> | <release>" ("none" for an empty press part); "none" for an empty side.
std::string macroSideText(const MacroSide &side);
// The text after "macro:" of a motion: "<neg>,<pos>"; a direction may be "none"; at least one needs steps.
bool parseMacroAction(const std::string &text, MacroAction *out, std::string *err);
std::string macroActionText(const MacroAction &action);  // canonical "<neg>,<pos>" (without "macro:")

// ---- load warning (tier B, docs/security.md section 7) -------------------------------------------------------------------
// The key presses of the macro that the loader names in its plain warning: every Down chord that is not a tier A key
// (letters, digits, modifiers, function keys, ...; keyAnalogAllowed in modes.hpp). "" if there is none. No gate, only a warning.
std::string macroRiskText(const MacroSteps &steps);
std::string macroRiskText(const MacroSide &side);      // the press and the release part
std::string macroRiskText(const MacroAction &action);  // both directions

}  // namespace spn
