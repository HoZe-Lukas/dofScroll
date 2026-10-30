// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Modes as plugins: declarative files in <config dir>/dofscroll/modes/*.conf (file name without
// ".conf" = mode id, [a-z0-9_-]{1,32}). No recompilation needed.
// A mode defines: (1) what the three channels (vertical, horizontal, twist) trigger,
// (2) what each button (button index) does, (3) settings overrides (the same keys as in a
// profile, e.g. SCROLL_H_SENS). Exactly ONE mode is active: the one chosen by the config key
// MODE in the resolved profile, otherwise "normal". MODE may appear in any section ([global],
// [device:*], [app:*]; later wins, app before device before global).
//
// File format (like the main config: "KEY = value", '#'/';' comments, no sections):
//   NAME = Timeline (scrub/single frames)           # display name (tray), optional
//   DESCRIPTION = Video editing, e.g. DaVinci ...   # optional
//   V_ACTION = none                                 # vertical channel   (SCROLL_V)
//   H_ACTION = keys:left,right                      # horizontal channel (SCROLL_H)
//   TWIST_ACTION = keys:left,right                  # twist channel      (ZOOM)
//   LIFT_ACTION = none                              # lift / press motion (LIFT_*, y with spacenavd); default none
//   BUTTON_1 = key:space                            # button 1 holds the space bar
//   BUTTON_2 = click
//   SCROLL_H_SENS = 4                               # any settings key (applyKey)
//
// Channel actions:  default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos>
//   default = the channel's normal behavior (V: wheel_v, H: wheel_h, twist: zoom with
//   dominance detection). zoom = hold Ctrl + mouse wheel. keys:<neg>,<pos> = key presses
//   (neg for negative, pos for positive deflection; rate proportional to the deflection),
//   key names from keys.hpp. Per side a key, a chord or "none". keys: is analog and only accepted for keys that are safe
//   to repeat (keyAnalogAllowed: "this key can only be used digitally, use step:"); step:<neg>,<pos> is the digital one
//   (one press per push, any key; a side may also be click, rightclick or middleclick: one mouse click per push) and
//   hold:<neg>,<pos> (a key, chord or mouse button is held while the motion is deflected, released at half the trigger).
//   macro:<neg>,<pos> = per side a macro (macro.hpp): key down, key up and pause steps that run once per push, with the
//   digital trigger of step: (a side may be none; "<press> | <release>" adds a macro for the release of the trigger).
//   Rate limits and trigger per motion: <P>KEY_RATE_MIN / <P>KEY_RATE_MAX / <P>TRIGGER
//   (config.hpp KeyTuning, key_engine.hpp).
// Split push and tilt (optional, per scroll channel): V_PUSH_ACTION, V_TILT_ACTION, H_PUSH_ACTION and
//   H_TILT_ACTION (same grammar). The vertical channel mixes push forward/back (z) and tilt forward/back (rx),
//   the horizontal channel push sideways (x) and tilt sideways (rz). A mode that sets at least one split key of a
//   channel runs that channel as two independent sub-channels: the push part is the translation terms (x, y, z)
//   of the channel's mix, the tilt part the rotation terms (rx, rz), weights and signs kept (defaults: V push =
//   z, V tilt = rx, H push = x, H tilt = -rz; ry belongs to neither). A part without its own key uses the
//   channel's V_ACTION / H_ACTION; an explicit "default" on a part is the channel's normal behavior (wheel).
//   Each part uses the channel's sens, dead zone, curve, smoothing and sign; SCROLL_<V|H>_<PUSH|TILT>_<SENS|
//   DEADZONE|SIGN|CURVE|SMOOTH_MS> override single values with absolute values (config.hpp, PartTuning).
//   A mode without split keys behaves exactly as before.
// Lift / press: LIFT_ACTION takes default | none | wheel_v | wheel_h | zoom | step:<neg>,<pos>; "default" is none (the
//   cap is lifted when it is grabbed, so nothing fires unless a mode or profile asks for it). An analog keys:<neg>,<pos>
//   is rejected, use step: (one tap per push). Tuning: LIFT_MIX (default y), LIFT_SENS, LIFT_DEADZONE (default 80),
//   LIFT_SIGN, LIFT_CURVE, LIFT_SMOOTH_MS, valid in profiles and in mode files like the other channel keys.
// Buttons are numbered from 1 (BUTTON_1 .. BUTTON_32), exactly like BUTTON_<n> in the main config;
// BUTTON_0 is rejected. Internally button n is device bit n-1 (spnav_event_button.bnum).
// Button actions: none | click | rightclick | middleclick | nav | key:<name> | macro:<steps> | mode:<id> | mode:next |
//                 mode:prev | toggle:nav | toggle:mode:<id> | hold:mode:<id> | buttonmode:<id>
//   macro:<steps> = a macro (macro.hpp) that runs once when the button is pressed; "macro:<press> | <release>" runs a second
//   one when it is released. One macro runs at a time (a trigger while one runs is ignored); everything it holds is released
//   at its end and on every abort.
//   click/rightclick/middleclick = hold the mouse button while the button is held;
//   key = hold the key; nav = while held, the V/H channels send arrow keys instead of scroll.
//   mode:<id> = switch to the mode <id> when the button is PRESSED (not on release);
//   mode:next:<id>,<id>,... / mode:prev:<id>,... = the same cycle through exactly this list of mode ids
//   in this order (wrapping; if the effective mode is not in the list next goes to the first entry and
//   prev to the last; unknown ids are skipped at runtime with one warning per load). This form is
//   what a button mode with a CYCLE setting expands to (button_modes.hpp); at most 32 distinct ids.
//   mode:next = cycle through all loaded modes in alphabetical order of id (built-ins
//   included), wrapping around; mode:prev is the mirror (the previous id, wrapping from the first
//   to the last; stateless, no history). "next" and "prev" are reserved targets: a mode file
//   called next.conf or prev.conf still loads, but mode:next / mode:prev always mean the cycle
//   action. The switch is a runtime override of the mode chosen by
//   profile resolution: it is sticky (also across profile/app changes) until the daemon
//   restarts, the config or the modes directory is reloaded, or another switch happens.
//   All held keys/buttons are released on every switch; the status file "mode=" line
//   shows the effective mode. An id that matches no loaded mode is rejected with a
//   warning when the modes are loaded (the binding is dropped); "next" and "prev" are always valid.
//   toggle:nav = on every PRESS the navigation mode (arrow keys instead of scroll) is switched on
//   or off, like holding a nav button but latched. toggle:mode:<id> = the first press switches to
//   the mode <id> (like mode:<id>), the next press while <id> is the effective mode drops that
//   switch and returns to the mode that profile resolution gives. Both are state that is cleared
//   by a reload (also "Release all keys" in the tray), and by entering suspend/disabled; the
//   status file shows it in the "toggle=" line. "next" and "prev" are not valid in
//   toggle:mode:<id> (there is no single mode to return from).
//   hold:mode:<id> = the mode <id> is effective while the button is held; on release the state
//   from before the press comes back (a mode:<id> / toggle:mode:<id> override that was in
//   effect, else the mode of the profile). Any other mode switch during the hold ends it without
//   restoring; a reload, suspend/disabled and an unplugged device end it too. "next" and "prev" are
//   not valid; an unknown id is dropped with a warning when the modes are loaded.
// A mode whose channel actions (V, H, twist, lift) are all "default" (and that sets no split key) runs on the normal path (incl.
// dominance-based zoom, NAV_BUTTON/CLICK_BUTTON from the settings).
//
// Built in (overridable by a file of the same name): "normal" (all default, no buttons),
// "off" (all none: switches the device off), "timeline" (V none, H and TWIST keys:left,right).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "builtin_library.hpp"
#include "chord.hpp"
#include "keys.hpp"
#include "macro.hpp"

namespace spn {

// Keys: a rate of key taps (keys:<neg>,<pos>); Step: one tap per push (step:<neg>,<pos>), both use neg/pos.
// Hold: hold:<neg>,<pos> (stage 4 of docs/plan-motion-functions.md): per side a key, a chord or a mouse button is held while the
// motion is deflected beyond the trigger and released when it has fallen back to half of it (digital only).
// PointerX / PointerY: relative pointer movement (pointer_x, pointer_y), continuous, no keys (src/pointer_engine.hpp).
// Macro: macro:<neg>,<pos> (stage 7b, macro.hpp): per side a list of key down, key up and pause steps that runs once per
// trigger, with the digital trigger of step: (digital only).
enum class ChannelActionType { Default, Off, WheelV, WheelH, Zoom, Keys, Step, Hold, Macro, PointerX, PointerY };
// The mouse buttons a side can name (numbering of Output::mouseButton): click = 1, middleclick = 2, rightclick = 3.
constexpr uint8_t kMouseLeft = 1, kMouseMiddle = 2, kMouseRight = 3;
struct ChannelAction {
    ChannelActionType type = ChannelActionType::Default;
    Key neg = Key::Up, pos = Key::Down;  // only for Keys, Step and Hold: the key of the chord
    bool usesKeys() const {
        return type == ChannelActionType::Keys || type == ChannelActionType::Step || type == ChannelActionType::Hold;
    }
    ChordMods negMods = ChordMods(), posMods = ChordMods();         // only for Keys, Step and Hold: modifiers held while the key is tapped
    bool negNone = false, posNone = false;  // only for Keys, Step and Hold: "none" for that side, nothing is pressed in that direction
    // Only for Step (one click per press) and Hold (the button is held): a mouse button instead of a key on that side,
    // 0 = a key (or none), else kMouseLeft / kMouseMiddle / kMouseRight. Never with the analog Keys.
    uint8_t negMouse = 0, posMouse = 0;
    // Only for Macro: the macros of the two directions (never null for a parsed Macro). Shared, so copying an action stays cheap.
    std::shared_ptr<const MacroAction> macro = nullptr;
    bool operator==(const ChannelAction &o) const {
        if (type == ChannelActionType::Macro) return o.type == type && (macro == o.macro || (macro && o.macro && *macro == *o.macro));
        return type == o.type &&
               (!usesKeys() ||
                (negNone == o.negNone && posNone == o.posNone && negMouse == o.negMouse && posMouse == o.posMouse &&
                 (negNone || negMouse || (neg == o.neg && negMods == o.negMods)) &&
                 (posNone || posMouse || (pos == o.pos && posMods == o.posMods))));
    }
};

enum class ButtonActionType { Unbound, Click, RightClick, MiddleClick, Nav, Key, ModeSwitch, ToggleNav, ToggleMode, ButtonMode, HoldMode, Macro };
struct ButtonBinding {
    int button = 1;                       // button number, 1-based (device bit index = button - 1), 1..32
    ButtonActionType type = ButtonActionType::Unbound;
    Key key = Key::Space;                 // only for Key
    std::string mode;                     // ModeSwitch: target id or "next"; ToggleMode: target id; ButtonMode: button mode id; HoldMode: target id
    ChordMods mods;                       // only for Key: modifiers of a chord (key:ctrl+0); a chord is tapped once per press
    std::shared_ptr<const MacroSide> macro = nullptr;  // only for Macro: the macro of the press and, optional, of the release
};

// The two parts of a split scroll channel (V_PUSH_ACTION / V_TILT_ACTION, H_PUSH_ACTION / H_TILT_ACTION).
// A part whose key is absent (pushSet / tiltSet false) falls back to the channel's own action.
struct SplitChannel {
    ChannelAction push, tilt;
    bool pushSet = false, tiltSet = false;
    bool active() const { return pushSet || tiltSet; }
    bool operator==(const SplitChannel &o) const {
        return pushSet == o.pushSet && tiltSet == o.tiltSet && (!pushSet || push == o.push) && (!tiltSet || tilt == o.tilt);
    }
    // The action a part runs: its own if set, otherwise the channel's `parent`.
    const ChannelAction &pushAction(const ChannelAction &parent) const { return pushSet ? push : parent; }
    const ChannelAction &tiltAction(const ChannelAction &parent) const { return tiltSet ? tilt : parent; }
};

struct ModeDef {
    std::string id;           // id (file name without .conf, or built-in)
    std::string label;        // display name; fallback: id
    std::string description;  // optional
    std::vector<std::pair<std::string, std::string>> overrides;  // settings keys (validated)
    ChannelAction v, h, twist;
    ChannelAction lift;           // LIFT_ACTION (the lift / press motion); "default" = none
    SplitChannel vSplit, hSplit;  // optional split of the V / H channel into push and tilt
    std::vector<ButtonBinding> buttons;
    bool builtin = false;

    bool allDefaultChannels() const {
        return v.type == ChannelActionType::Default && h.type == ChannelActionType::Default &&
               twist.type == ChannelActionType::Default &&
               (lift.type == ChannelActionType::Default || lift.type == ChannelActionType::Off) && !vSplit.active() &&
               !hSplit.active();
    }
};

struct ModeSet {
    std::vector<ModeDef> modes;  // modes[0] is always "normal"
    // nullptr if unknown.
    const ModeDef *find(const std::string &id) const;
    const ModeDef &normal() const { return modes[0]; }
};

// Syntactically valid id ([a-z0-9_-]{1,32}).
bool validModeId(const std::string &id);

bool parseChannelAction(const std::string &text, ChannelAction *out, std::string *err);
// LIFT_ACTION: the grammar of parseChannelAction, but an analog "keys:<neg>,<pos>" is rejected (lifting the cap fires when
// the cap is grabbed, so a key repeat on it would type by accident); the digital "step:<neg>,<pos>" is allowed.
// "default" means none for the lift.
bool parseLiftAction(const std::string &text, ChannelAction *out, std::string *err);
// True when a key or chord may be pressed by the analog `keys:` action (tier A of docs/plan-motion-functions.md: navigation
// keys, ctrl or shift plus a navigation key, volume, media next and previous, plus and minus). Everything else (letters, digits,
// function keys, enter, delete, escape, space, alt or super chords, play_pause, a lone modifier) can only be used digitally.
bool keyAnalogAllowed(Key key, const ChordMods &mods);
// The canonical config text of a channel action ("default", "none", "wheel_v", "keys:ctrl+pageup,ctrl+pagedown", ...),
// the inverse of parseChannelAction.
std::string channelActionText(const ChannelAction &a);

// Cycle lists (mode:next:<ids> / button mode CYCLE).
constexpr size_t kMaxModeCycle = 32;
// Parses "id1, id2 id3" (comma and/or whitespace separated, ids lower-cased and checked with
// validModeId). Duplicates are dropped (their ids are appended to *dups if given). Empty list, more
// than kMaxModeCycle distinct ids or an invalid id -> false + *err.
bool parseModeCycleList(const std::string &text, std::vector<std::string> *out, std::string *err,
                        std::vector<std::string> *dups = nullptr);
// Splits a ModeSwitch target: "next" / "prev" (ids empty, true) or "next:a,b" / "prev:a,b".
// *forward = true for next. false if target is anything else (a plain mode id).
bool splitCycleTarget(const std::string &target, bool *forward, std::vector<std::string> *ids);

// modeId (optional) receives the target of mode:<id> / mode:next / toggle:mode:<id> and the button mode id of buttonmode:<id>. Only the syntax of the id is
// checked here; existence is checked by loadModes (the parser does not know the mode set).
// key:<name> is a plain key (held while the button is held); key:<mod>+...+<key> (zero to four of ctrl, shift,
// alt, super, then a key that is not a modifier, as in keys:) is a chord: *mods (optional) receives the
// modifiers (n = 0 for a plain key) and the chord is tapped once when the button goes down.
// macro:<side> (macro.hpp) is a button macro: *macro (optional) receives the parsed side; the canonical text is
// "macro:" + macroSideText(*macro).
bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err,
                       std::string *modeId = nullptr, ChordMods *mods = nullptr, MacroSide *macro = nullptr);
// Canonical config text of a key action: "key:space", "key:ctrl+0" (modifiers in the order written).
std::string keyActionText(Key key, const ChordMods &mods);
// True for a chord that the loader warns about in files of the user: it has alt or super, or ctrl/shift with a key that
// is not a navigation or zoom key (docs/modes.md, Safety). False for a plain key (mods.n == 0).
bool chordNeedsWarning(Key key, const ChordMods &mods);

// <config dir>/dofscroll/modes (config dir as in legacyConfigPath()).
std::string modesDir();

// Built-in modes + all valid *.conf in dir. A missing directory is not an error.
// Hard limits: <= 64 files, <= 64 KiB each, <= 128 overrides per file; regular files owned by
// the current user only (opened with O_NOFOLLOW, so symlinks are rejected). Faulty lines/files
// are skipped with a warning (warnings optional). A file of the same name replaces the
// built-in mode.
// library: replaces the embedded example files (a list as builtinModeFiles() returns); only for
// tests that vary the library at run time, nullptr = the embedded one.
ModeSet loadModes(const std::string &dir, std::vector<std::string> *warnings, const BuiltinFile *library = nullptr);

// Cheap identity of the directory contents (names + mtime + size) for auto-reload.
// Empty if the directory is missing.
std::string modesStamp(const std::string &dir);

}  // namespace spn
