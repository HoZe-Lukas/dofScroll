// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Press the key" of the visual joystick mode editor (issue #174, toolkit-free): turns what was pressed (a key
// name from keys.hpp and the held modifiers) into the chord text of a keys:/step: action ("ctrl+pageup"). Only
// keys dofscroll can send are accepted; a modifier on its own waits for the key that goes with it; anything
// else says so in words and leaves the chord as it was. The view maps the keys of the toolkit to the names.
//
//   captureChord("pageup", {ctrl:true})   // Chord "ctrl+pageup"
//   captureChord("ctrl", {})              // Waiting (a modifier alone)
//   captureChord("", {})                  // Unsupported
#pragma once
#include <string>

#include "keys.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct HeldMods {
    bool ctrl = false, shift = false, alt = false, super = false;
};

struct CaptureResult {
    enum Kind { Waiting, Chord, Unsupported } kind = Unsupported;
    std::string chord;    // Chord: "ctrl+shift+pageup", modifiers in the fixed order ctrl, shift, alt, super
    std::string message;  // Unsupported: what to do instead (English source text through `strings`)
};

inline bool isModifierName(const std::string &n) { return n == "ctrl" || n == "shift" || n == "alt" || n == "super"; }

inline CaptureResult captureChord(const std::string &keyName, const HeldMods &mods, const StringProvider &s) {
    CaptureResult r;
    Key k;
    if (keyName.empty() || !parseKeyName(keyName, &k)) {
        r.kind = CaptureResult::Unsupported;
        r.message = s.tr("dofscroll cannot send this key. Choose a key from the list instead.");
        return r;
    }
    if (isModifierName(keyName)) {
        r.kind = CaptureResult::Waiting;
        return r;
    }
    r.kind = CaptureResult::Chord;
    if (mods.ctrl) r.chord += "ctrl+";
    if (mods.shift) r.chord += "shift+";
    if (mods.alt) r.chord += "alt+";
    if (mods.super) r.chord += "super+";
    r.chord += keyName;
    return r;
}

}  // namespace spn::ui
