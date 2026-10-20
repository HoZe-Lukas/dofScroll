// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// GUI-free logic for the "Application modes" list of the tray settings window: one row
// per program ([app:*]) and per device ([device:*]) profile, each row choosing the mode
// (config key MODE) for that profile. Header-only; unit-tested in tests/test_mode_list.cpp.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"

namespace spn {
namespace modelist {

struct Row {
    Section::Kind kind = Section::APP;
    std::string pattern;
    std::string mode_id;  // effective mode of this profile (global default if not set)
};

struct Choice {
    std::string id;
    std::string label;  // display text
    bool known = true;  // false: mode file is missing (id kept so nothing is lost silently)
};

// Programs first, then devices, each group in file order. Global is not listed.
inline std::vector<Row> buildRows(const ProfileSet &ps) {
    std::vector<Row> rows;
    for (Section::Kind k : {Section::APP, Section::DEVICE})
        for (size_t i = 1; i < ps.sections.size(); ++i)
            if (ps.sections[i].kind == k)
                rows.push_back({k, ps.sections[i].pattern, ps.sectionSettings(i).mode});
    return rows;
}

// All loaded modes in load order. If current_id is not among them it is appended as an
// unknown entry (label = id) so the combo box can still show it.
inline std::vector<Choice> buildChoices(const ModeSet &ms, const std::string &current_id) {
    std::vector<Choice> out;
    for (const ModeDef &m : ms.modes) out.push_back({m.id, m.label.empty() ? m.id : m.label, true});
    if (!current_id.empty() && !ms.find(current_id)) out.push_back({current_id, current_id, false});
    return out;
}

inline int choiceIndex(const std::vector<Choice> &c, const std::string &id) {
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].id == id) return int(i);
    return -1;
}

// Display label of a mode id (label from the loaded modes, else the id itself).
inline std::string modeLabel(const ModeSet &ms, const std::string &id) {
    const ModeDef *m = ms.find(id);
    return m && !m->label.empty() ? m->label : id;
}

// Sets MODE in the raw key/value list of the profile (kind, pattern). Existing MODE
// entries are replaced (all of them), otherwise one is appended. false if the profile
// does not exist, is global, or the id is not a valid mode id.
inline bool setRowMode(ProfileSet &ps, Section::Kind kind, const std::string &pattern,
                       const std::string &mode_id) {
    if (kind == Section::GLOBAL || !validModeId(mode_id)) return false;
    size_t idx = ps.find(kind, pattern);
    if (idx == ProfileSet::npos || idx == 0) return false;
    bool found = false;
    for (auto &kv : ps.sections[idx].kv)
        if (kv.first == "MODE") {
            kv.second = mode_id;
            found = true;
        }
    if (!found) ps.sections[idx].kv.emplace_back("MODE", mode_id);
    return true;
}

// ---------------------------------------------------------------------------
// Quick mode menu of the tray icon: one radio item per loaded mode, the effective global
// mode selected.
// ---------------------------------------------------------------------------

struct MenuEntry {
    std::string id;
    std::string text;  // "<display name> (<id>)", or just the id if there is no distinct name
    bool active = false;
    bool known = true;  // false: the global MODE names a mode that is not loaded
};

// Effective global mode id (MODE in [global]; "normal" if unset or invalid).
inline std::string globalModeId(const ProfileSet &ps) {
    const std::string id = ps.sectionSettings(0).mode;
    return validModeId(id) ? id : std::string("normal");
}

// Load order (normal first, then the other built-ins and the mode files). If the global mode
// is not loaded, it is appended as an unknown, active entry so the selection stays visible.
inline std::vector<MenuEntry> buildMenuEntries(const ModeSet &ms, const std::string &current_id) {
    std::vector<MenuEntry> out;
    bool seen = false;
    for (const ModeDef &m : ms.modes) {
        MenuEntry e;
        e.id = m.id;
        e.text = (m.label.empty() || m.label == m.id) ? m.id : m.label + " (" + m.id + ")";
        e.active = m.id == current_id;
        seen = seen || e.active;
        out.push_back(e);
    }
    if (!seen && !current_id.empty()) out.push_back({current_id, current_id, true, false});
    return out;
}

// What picking a mode writes into [global]: "normal" is the default, so the MODE key is
// removed (all occurrences); any other valid id replaces/appends MODE. false: invalid id.
inline bool setGlobalMode(ProfileSet &ps, const std::string &mode_id) {
    if (!validModeId(mode_id)) return false;
    auto &kv = ps.sections[0].kv;
    if (mode_id == "normal") {
        for (size_t i = kv.size(); i-- > 0;)
            if (kv[i].first == "MODE") kv.erase(kv.begin() + long(i));
        return true;
    }
    bool found = false;
    for (auto &e : kv)
        if (e.first == "MODE") {
            e.second = mode_id;
            found = true;
        }
    if (!found) kv.emplace_back("MODE", mode_id);
    return true;
}

// ---------------------------------------------------------------------------
// Editing modes from the tray: which "edit" entries exist, template text for a built-in mode,
// and safe creation of a new mode file (never overwrites).
// ---------------------------------------------------------------------------

struct EditEntry {
    std::string id;
    std::string text;      // "<display name> (<id>)", or just the id
    bool has_file = true;  // false: built-in without a file -> "create editable copy"
};

// One entry per loaded mode, in load order. A mode loaded from a file has a file; a pure
// built-in (ModeDef::builtin) has none.
inline std::vector<EditEntry> buildEditEntries(const ModeSet &ms) {
    std::vector<EditEntry> out;
    for (const ModeDef &m : ms.modes) {
        EditEntry e;
        e.id = m.id;
        e.text = (m.label.empty() || m.label == m.id) ? m.id : m.label + " (" + m.id + ")";
        e.has_file = !m.builtin;
        out.push_back(e);
    }
    return out;
}

// <dir>/<id>.conf, or "" if the id is not a valid mode id or dir is empty.
inline std::string modeFilePath(const std::string &dir, const std::string &id) {
    if (dir.empty() || !validModeId(id)) return std::string();
    return dir + "/" + id + ".conf";
}

inline std::string chordText(Key key, const ChordMods &mods) {
    std::string t;
    for (uint8_t i = 0; i < mods.n; ++i) t += std::string(keyName(mods.k[i])) + "+";
    return t + keyName(key);
}

inline std::string channelActionText(const ChannelAction &a) {
    switch (a.type) {
        case ChannelActionType::Default: return "default";
        case ChannelActionType::Off: return "none";
        case ChannelActionType::WheelV: return "wheel_v";
        case ChannelActionType::WheelH: return "wheel_h";
        case ChannelActionType::Zoom: return "zoom";
        case ChannelActionType::Step: return "step:" + chordText(a.neg, a.negMods) + "," + chordText(a.pos, a.posMods);
        case ChannelActionType::Keys: return "keys:" + chordText(a.neg, a.negMods) + "," + chordText(a.pos, a.posMods);
    }
    return "default";
}

// Config text of a button binding ("click", "key:space", "mode:next", ...; "none" if unbound).
inline std::string buttonActionText(const ButtonBinding &b) {
    switch (b.type) {
        case ButtonActionType::Unbound: return "none";
        case ButtonActionType::Click: return "click";
        case ButtonActionType::RightClick: return "rightclick";
        case ButtonActionType::MiddleClick: return "middleclick";
        case ButtonActionType::Nav: return "nav";
        case ButtonActionType::Key: return std::string("key:") + keyName(b.key);
        case ButtonActionType::ModeSwitch: return "mode:" + b.mode;
        case ButtonActionType::ToggleNav: return "toggle:nav";
        case ButtonActionType::ToggleMode: return "toggle:mode:" + b.mode;
        case ButtonActionType::ButtonMode: return "buttonmode:" + b.mode;
        case ButtonActionType::HoldMode: return "hold:mode:" + b.mode;
    }
    return "none";
}

// Template mode file text for a mode (normally a built-in): NAME/DESCRIPTION, the channel
// actions, the buttons and the overrides, with explanatory comments. loadModes parses it back
// into an equivalent mode.
inline std::string modeTemplate(const ModeDef &m) {
    std::string t;
    t += "# Mode \"" + m.id + "\" - editable copy. Save the file; the daemon reloads it by itself.\n";
    t += "# A file of the same name replaces the built-in mode. Documentation: docs/modes.md\n";
    t += "# Channel actions: default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos> | step:<neg>,<pos> (keys may be chords: ctrl+pageup)\n";
    t += "# Button actions: none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next | mode:prev |\n";
    t += "#                 toggle:nav | toggle:mode:<id> | hold:mode:<id> | buttonmode:<id>\n";
    t += "# Buttons are numbered from 1: BUTTON_1 = key:space\n\n";
    t += "NAME = " + (m.label.empty() ? m.id : m.label) + "\n";
    if (!m.description.empty()) t += "DESCRIPTION = " + m.description + "\n";
    t += "V_ACTION = " + channelActionText(m.v) + "\n";
    t += "H_ACTION = " + channelActionText(m.h) + "\n";
    t += "TWIST_ACTION = " + channelActionText(m.twist) + "\n";
    if (m.vSplit.pushSet) t += "V_PUSH_ACTION = " + channelActionText(m.vSplit.push) + "\n";
    if (m.vSplit.tiltSet) t += "V_TILT_ACTION = " + channelActionText(m.vSplit.tilt) + "\n";
    if (m.hSplit.pushSet) t += "H_PUSH_ACTION = " + channelActionText(m.hSplit.push) + "\n";
    if (m.hSplit.tiltSet) t += "H_TILT_ACTION = " + channelActionText(m.hSplit.tilt) + "\n";
    for (const ButtonBinding &b : m.buttons) {
        if (b.type == ButtonActionType::Unbound) continue;
        t += "BUTTON_" + std::to_string(b.button) + " = " + buttonActionText(b) + "\n";
    }
    for (const auto &kv : m.overrides) t += kv.first + " = " + kv.second + "\n";
    return t;
}

// Creates <dir>/<id>.conf with the given content (private, user only), exclusively: an existing
// file or link is never touched (fs::createFileExclusive publishes the complete content without
// overwriting). The directory is created private if missing. On success *path receives the file
// path. false + *err otherwise.
inline bool createModeFile(const std::string &dir, const std::string &id, const std::string &content,
                           std::string *path, std::string *err) {
    auto fail = [&](const std::string &m) {
        if (err) *err = m;
        return false;
    };
    const std::string p = modeFilePath(dir, id);
    if (p.empty()) return fail("invalid mode id or directory");
    std::string e;
    if (!fs::mkdirsPrivate(dir, &e)) return fail("mkdir: " + e);
    if (fs::pathExists(p)) return fail("file already exists");
    if (!fs::createFileExclusive(p, content)) return fail("cannot create file");
    if (path) *path = p;
    return true;
}

}  // namespace modelist
}  // namespace spn
