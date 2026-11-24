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
//   tap:<chord>  press and release in one go: the modifiers down in the order written, the key down and up, the modifiers up
//                in reverse order (tap:ctrl+c). "+a -a" stays valid and does the same in two steps.
//   click | rightclick | middleclick           a mouse button down and up
//   +click | +rightclick | +middleclick        a mouse button down, stays held until the matching -<name> step
//   -click | -rightclick | -middleclick        a mouse button up
//   paste:"text" puts the text on the clipboard and pastes it (Ctrl+V, Cmd+V on macOS); the clipboard text that was there is put
//                back afterwards (text only). The text is taken as written, upper and lower case and blanks are kept; there are no
//                escape sequences. Refused: control characters (ESC, tab, line breaks, NUL, DEL), the backslash, the double
//                quote, '#' (comment start in config files), invisible and direction-changing characters, invalid UTF-8;
//                1..kMaxMacroPasteBytes bytes. ',' and '|' are fine inside the quotes. One step, whatever the length.
//   repeat:<n>   start of a block that runs n times (1..kMaxMacroRepeat), up to the next "end"; blocks do not nest. Every
//   end          pass of a block takes at least one daemon tick. "repeat" without a count (until the trigger is released,
//                bounded by kMaxMacroRunSeconds) is only valid in the press part of a side with the while_held prefix.
// A key name is one of the closed list in keys.hpp. Whatever a macro still holds (keys and mouse buttons) is released when it
// ends (and when it is aborted), so a press-only macro such as "+ctrl" is valid.
//
// Action text (the part after "macro:"):
//   BUTTON_<n> = macro:<side>                 device button: <side> runs when the button is pressed
//   <MOTION>_ACTION = macro:<neg>,<pos>       motion: one <side> per direction, "none" for a direction without macro
//   <side> = [while_held] <part>              the macro that runs when the trigger fires
//          | <part> | <part>                  press part | release part: the second runs when the trigger is released
//                                             (button: released; motion: back at half of the trigger)
//   <part> = none | <step> { <step> }         "none" = nothing in that place; a side needs at least one part with steps
//   while_held: the press part stops as soon as the trigger is released (a motion: back at half of the trigger): what it holds is
//   released at once and the release part, if any, runs. Without it the press part always runs to its end.
// Example (select all, copy, then type "ab" with shift held):
//   macro:+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b
// Digital only. At most kMaxMacroSteps steps per part AFTER the repeat blocks are expanded (wait steps count), a run lasts at most
// kMaxMacroRunSeconds. Further triggers while a macro runs are ignored until it is done.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chord.hpp"
#include "keys.hpp"

namespace spn {

constexpr size_t kMaxMacroSteps = 256;              // steps per macro part after the repeat blocks are expanded
constexpr int kMaxMacroRepeat = 1000;               // the count of a repeat block
constexpr int kMaxMacroRunSeconds = 600;            // a run longer than this is ended (what it holds is released)
constexpr int kMinMacroWaitMs = 1, kMaxMacroWaitMs = 60000;
constexpr int kDefaultMacroWaitMs = 30;             // the pause the recorder puts between steps when times are not recorded
constexpr size_t kMaxMacroPasteBytes = 1024;        // the text of a paste: step
constexpr size_t kMaxMacroKeysPerStep = kMaxChordMods + 1;  // the modifiers of a chord plus its key

struct MacroStep {
    enum class Kind : uint8_t { Down, Up, Wait, Tap, MouseDown, MouseUp, MouseTap, RepeatBegin, RepeatEnd, Paste };
    Kind kind = Kind::Wait;
    Key key = Key::Space;   // Down, Up, Tap: the chord's key (the last name of the chord)
    ChordMods mods;         // Down, Up, Tap: its modifiers
    int waitMs = 0;         // Wait
    uint8_t mouse = 1;      // MouseDown, MouseUp, MouseTap: 1 left, 2 middle, 3 right (the numbering of Output::mouseButton)
    std::string text;       // Paste: the text (UTF-8, no control or spoofing characters, no '\\' or '"')
    int count = 0;          // RepeatBegin: the number of passes; 0 = until the trigger is released (while_held only)

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
    static MacroStep tap(Key k, const ChordMods &m = ChordMods()) {
        MacroStep s = down(k, m);
        s.kind = Kind::Tap;
        return s;
    }
    static MacroStep mouseStep(Kind kind, int button) {
        MacroStep s;
        s.kind = kind;
        s.mouse = static_cast<uint8_t>(button);
        return s;
    }
    static MacroStep repeatBegin(int n) {
        MacroStep s;
        s.kind = Kind::RepeatBegin;
        s.count = n;
        return s;
    }
    static MacroStep paste(std::string t) {
        MacroStep s;
        s.kind = Kind::Paste;
        s.text = std::move(t);
        return s;
    }
    static MacroStep repeatEnd() {
        MacroStep s;
        s.kind = Kind::RepeatEnd;
        return s;
    }
    bool operator==(const MacroStep &o) const {
        if (kind != o.kind) return false;
        switch (kind) {
            case Kind::Wait: return waitMs == o.waitMs;
            case Kind::MouseDown:
            case Kind::MouseUp:
            case Kind::MouseTap: return mouse == o.mouse;
            case Kind::RepeatBegin: return count == o.count;
            case Kind::RepeatEnd: return true;
            case Kind::Paste: return text == o.text;
            default: return key == o.key && mods.n == o.mods.n && mods == o.mods;
        }
    }
    bool operator!=(const MacroStep &o) const { return !(*this == o); }
};
using MacroSteps = std::vector<MacroStep>;

// What one trigger runs: the macro of the press (or of the digital trigger) and, optional, the macro of the release.
struct MacroSide {
    MacroSteps press, release;
    bool whileHeld = false;  // the press part ends when the trigger is released (needs a press part)
    bool empty() const { return press.empty() && release.empty(); }
    bool operator==(const MacroSide &o) const { return press == o.press && release == o.release && whileHeld == o.whileHeld; }
};
// The macro of a motion: one side per direction.
struct MacroAction {
    MacroSide neg, pos;
    bool operator==(const MacroAction &o) const { return neg == o.neg && pos == o.pos; }
};

// "" if the text can be pasted by a paste: step, else the reason with the byte position of the first offender (see the grammar above).
std::string macroPasteTextProblem(const std::string &text);
// true if the action text has a ',' outside of double quotes (a button macro is one side, without a comma).
bool macroTextHasTopLevelComma(const std::string &text);

// ---- one step -------------------------------------------------------------------------------------------------------
// Parses "+ctrl+c", "-a", "wait:30", "tap:ctrl+c", "click", "+rightclick", "repeat:3", "repeat", "end" (ASCII case folded) or paste:"text" (text kept as written). false + *err (a message for the user) otherwise.
bool parseMacroStep(const std::string &text, MacroStep *out, std::string *err);
// Canonical text of a step: "+ctrl+c", "-a", "wait:30", "tap:a", "click", "+click", "repeat:3" ("repeat" for count 0), "end". The inverse of parseMacroStep.
std::string macroStepText(const MacroStep &step);
// The keys a Down, Up or Tap step presses, in the order written (the modifiers, then the key). Returns the count (0 for every other step).
// out must hold kMaxMacroKeysPerStep entries.
size_t macroStepKeys(const MacroStep &step, Key *out);

// ---- a list of steps ------------------------------------------------------------------------------------------------
// Parses steps separated by spaces. "" and "none" give an empty list. Checks the grammar of every step and the limits
// (macroStepsProblem). false + *err names the step ("step 3 ('+foo'): unknown key 'foo'"). allowInfinite: a "repeat" without a
// count is accepted (the press part of a while_held side).
bool parseMacroSteps(const std::string &text, MacroSteps *out, std::string *err, bool allowInfinite = false);
// Canonical text: the steps separated by one space; an empty list gives "none".
std::string macroStepsText(const MacroSteps &steps);
// Validation of a list built in memory (the dialog): "" if it is valid, else the message: more than kMaxMacroSteps steps after
// the repeat blocks are expanded, a pause outside kMinMacroWaitMs..kMaxMacroWaitMs, a repeat block that is nested, empty, not
// closed or has a count outside 1..kMaxMacroRepeat (0 only when allowInfinite). An empty list is valid here (a side decides
// whether it needs steps).
std::string macroStepsProblem(const MacroSteps &steps, bool allowInfinite = false);
// The number of steps after the repeat blocks are expanded (the markers themselves do not count; a block without count counts once).
size_t macroExpandedSize(const MacroSteps &steps);

// ---- a side and a motion action -------------------------------------------------------------------------------------
// "[while_held] <part>" or "[while_held] <part> | <part>" (see above); at least one part must have steps; while_held needs steps
// in the press part. Spaces around '|' are optional.
bool parseMacroSide(const std::string &text, MacroSide *out, std::string *err);
// Canonical: "[while_held ]<press>" or "[while_held ]<press> | <release>" ("none" for an empty press part); "none" for an empty side.
std::string macroSideText(const MacroSide &side);
// The text after "macro:" of a motion: "<neg>,<pos>"; a direction may be "none"; at least one needs steps.
bool parseMacroAction(const std::string &text, MacroAction *out, std::string *err);
std::string macroActionText(const MacroAction &action);  // canonical "<neg>,<pos>" (without "macro:")

// ---- load warning (tier B, docs/security.md section 7) -------------------------------------------------------------------
// The key presses of the macro that the loader names in its plain warning: every Down or Tap chord and every mouse click that is not a tier A key
// (tier A = safe to repeat: arrows, Page up/down, Home, End, Tab, plus, minus, volume and track keys, see keyAnalogAllowed in
// modes.hpp; so letters, digits, function keys, Enter, Esc and modifiers are named). "" if there is none. No gate, only a warning.
std::string macroRiskText(const MacroSteps &steps);
std::string macroRiskText(const MacroSide &side);      // the press and the release part
std::string macroRiskText(const MacroAction &action);  // both directions

}  // namespace spn
