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

}  // namespace modelist
}  // namespace spn
