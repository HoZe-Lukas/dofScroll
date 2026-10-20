// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Button modes: a named button behavior with its own file <config dir>/dofscroll/buttons/<id>.conf.
//
//   NAME = Navigation latch         # shown in drop-downs; fallback: the id
//   DESCRIPTION = optional text
//   ACTION = toggle:nav             # required: one button action as parseButtonAction accepts it
//   CYCLE = video, slides, map      # optional, only with ACTION = mode:next or mode:prev
//
// CYCLE lists the joystick mode ids (comma separated, max. 32, duplicates dropped with a warning)
// that the button cycles through, in this order, wrapping around: next = the following entry (the
// first one when the current mode is not in the list), prev = the preceding one (the last one when
// it is not in the list). Ids that match no loaded mode are skipped at runtime (one warning per
// load). Without CYCLE, mode:next / mode:prev cycle through all loaded modes alphabetically. For
// both directions use two button modes. CYCLE with any other ACTION is ignored with a warning.
//
// A button mode is used with the button action "buttonmode:<id>" (BUTTON_<n> in [global], [device:*],
// [app:*] and mode files). The daemon core expands the reference at runtime (expandSettings,
// expandMode); the expansion is never written back to a config file. ACTION must not itself be a
// buttonmode: reference. Limits and file safety are those of mode files (conf_dir.hpp).
#pragma once
#include <set>
#include <string>
#include <vector>

#include "config.hpp"
#include "modes.hpp"

namespace spn {

struct ButtonMode {
    std::string id;
    std::string label;        // NAME, fallback id
    std::string description;
    std::string action;       // canonical action text
    std::string path;
    ButtonActionType type = ButtonActionType::Unbound;
    Key key = Key::Space;
    std::string target;       // mode id for ModeSwitch / ToggleMode ("next:a,b" with a cycle list)
    std::vector<std::string> cycle;  // CYCLE ids (validated, deduplicated); empty = all modes
};

struct ButtonModeSet {
    std::vector<ButtonMode> modes;  // load (name) order
    const ButtonMode *find(const std::string &id) const;
};

// <config dir>/dofscroll/buttons
std::string buttonModesDir();

// Parses the text of one file (id and path already set in *out). false: the file is unusable
// (no valid ACTION); the reason is in warnings. Other problems are warnings only.
bool parseButtonModeText(const std::string &text, ButtonMode *out, std::vector<std::string> *warnings);

// All valid <id>.conf of dir. A missing directory is no error.
ButtonModeSet loadButtonModes(const std::string &dir, std::vector<std::string> *warnings);
std::string buttonModesStamp(const std::string &dir);

// Replaces buttonmode:<id> in the BUTTON_<n> values of s by the action of that button mode. An
// unknown id sets the button to "none" and is added to *missing (the caller warns once per id).
void expandSettings(Settings &s, const ButtonModeSet &set, std::set<std::string> *missing);
// The same for the bindings of a mode (an unknown id leaves the button unbound, so the mode's
// binding still wins over the profile's, as before).
ModeDef expandMode(const ModeDef &m, const ButtonModeSet &set, std::set<std::string> *missing);

}  // namespace spn
