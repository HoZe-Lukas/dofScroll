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
//   (one press per push, any key). Rate limits and trigger per motion: <P>KEY_RATE_MIN / <P>KEY_RATE_MAX / <P>TRIGGER
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
// Button actions: none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next | mode:prev |
//                 toggle:nav | toggle:mode:<id> | hold:mode:<id> | buttonmode:<id>
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
#include <string>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "builtin_library.hpp"
#include "keys.hpp"

namespace spn {

// Modifiers of a key chord (keys:ctrl+pageup,...), in the order written; at most kMaxChordMods.
constexpr size_t kMaxChordMods = 4;
struct ChordMods {
    Key k[kMaxChordMods] = {Key::Ctrl, Key::Ctrl, Key::Ctrl, Key::Ctrl};
    uint8_t n = 0;
    // Order does not matter for equality: one bit per modifier key.
    unsigned mask() const {
        unsigned m = 0;
        for (uint8_t i = 0; i < n; ++i)
            m |= k[i] == Key::Ctrl ? 1u : k[i] == Key::Shift ? 2u : k[i] == Key::Alt ? 4u : 8u;
        return m;
    }
    bool operator==(const ChordMods &o) const { return mask() == o.mask(); }
};

// Keys: a rate of key taps (keys:<neg>,<pos>); Step: one tap per push (step:<neg>,<pos>), both use neg/pos.
enum class ChannelActionType { Default, Off, WheelV, WheelH, Zoom, Keys, Step };
struct ChannelAction {
    ChannelActionType type = ChannelActionType::Default;
    Key neg = Key::Up, pos = Key::Down;  // only for Keys and Step: the key of the chord
    bool usesKeys() const { return type == ChannelActionType::Keys || type == ChannelActionType::Step; }
    ChordMods negMods = ChordMods(), posMods = ChordMods();         // only for Keys and Step: modifiers held while the key is tapped
    bool negNone = false, posNone = false;  // only for Keys and Step: "none" for that side, nothing is pressed in that direction
    bool operator==(const ChannelAction &o) const {
        return type == o.type &&
               (!usesKeys() ||
                (negNone == o.negNone && posNone == o.posNone && (negNone || (neg == o.neg && negMods == o.negMods)) &&
                 (posNone || (pos == o.pos && posMods == o.posMods))));
    }
};

enum class ButtonActionType { Unbound, Click, RightClick, MiddleClick, Nav, Key, ModeSwitch, ToggleNav, ToggleMode, ButtonMode, HoldMode };
struct ButtonBinding {
    int button = 1;                       // button number, 1-based (device bit index = button - 1), 1..32
    ButtonActionType type = ButtonActionType::Unbound;
    Key key = Key::Space;                 // only for Key
    std::string mode;                     // ModeSwitch: target id or "next"; ToggleMode: target id; ButtonMode: button mode id; HoldMode: target id
    ChordMods mods;                       // only for Key: modifiers of a chord (key:ctrl+0); a chord is tapped once per press
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
bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err,
                       std::string *modeId = nullptr, ChordMods *mods = nullptr);
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
