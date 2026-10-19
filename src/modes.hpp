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
//   BUTTON_1 = key:space                            # button 1 holds the space bar
//   BUTTON_2 = click
//   SCROLL_H_SENS = 4                               # any settings key (applyKey)
//
// Channel actions:  default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos>
//   default = the channel's normal behavior (V: wheel_v, H: wheel_h, twist: zoom with
//   dominance detection). zoom = hold Ctrl + mouse wheel. keys:<neg>,<pos> = key presses
//   (neg for negative, pos for positive deflection; rate proportional to the deflection),
//   key names from keys.hpp.
// Buttons are numbered from 1 (BUTTON_1 .. BUTTON_32), exactly like BUTTON_<n> in the main config;
// BUTTON_0 is rejected. Internally button n is device bit n-1 (spnav_event_button.bnum).
// Button actions: none | click | rightclick | middleclick | nav | key:<name>
//   click/rightclick/middleclick = hold the mouse button while the button is held;
//   key = hold the key; nav = while held, the V/H channels send arrow keys instead of scroll.
// A mode whose three channel actions are all "default" runs on the normal path (incl.
// dominance-based zoom, NAV_BUTTON/CLICK_BUTTON from the settings).
//
// Built in (overridable by a file of the same name): "normal" (all default, no buttons),
// "off" (all none: switches the device off), "timeline" (V none, H and TWIST keys:left,right).
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "keys.hpp"

namespace spn {

enum class ChannelActionType { Default, Off, WheelV, WheelH, Zoom, Keys };
struct ChannelAction {
    ChannelActionType type = ChannelActionType::Default;
    Key neg = Key::Up, pos = Key::Down;  // only for Keys
    bool operator==(const ChannelAction &o) const {
        return type == o.type && (type != ChannelActionType::Keys || (neg == o.neg && pos == o.pos));
    }
};

enum class ButtonActionType { Unbound, Click, RightClick, MiddleClick, Nav, Key };
struct ButtonBinding {
    int button = 1;                       // button number, 1-based (device bit index = button - 1), 1..32
    ButtonActionType type = ButtonActionType::Unbound;
    Key key = Key::Space;                 // only for Key
};

struct ModeDef {
    std::string id;           // id (file name without .conf, or built-in)
    std::string label;        // display name; fallback: id
    std::string description;  // optional
    std::vector<std::pair<std::string, std::string>> overrides;  // settings keys (validated)
    ChannelAction v, h, twist;
    std::vector<ButtonBinding> buttons;
    bool builtin = false;

    bool allDefaultChannels() const {
        return v.type == ChannelActionType::Default && h.type == ChannelActionType::Default &&
               twist.type == ChannelActionType::Default;
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
bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err);

// <config dir>/dofscroll/modes (config dir as in legacyConfigPath()).
std::string modesDir();

// Built-in modes + all valid *.conf in dir. A missing directory is not an error.
// Hard limits: <= 64 files, <= 64 KiB each, <= 128 overrides per file; regular files owned by
// the current user only (opened with O_NOFOLLOW, so symlinks are rejected). Faulty lines/files
// are skipped with a warning (warnings optional). A file of the same name replaces the
// built-in mode.
ModeSet loadModes(const std::string &dir, std::vector<std::string> *warnings);

// Cheap identity of the directory contents (names + mtime + size) for auto-reload.
// Empty if the directory is missing.
std::string modesStamp(const std::string &dir);

}  // namespace spn
