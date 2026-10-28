// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Undo of "Remove profile": a copy of the profile (kind, name, all its settings and its place in the order)
// is taken before the removal; restoreProfile() puts it back into the model, and the autosave writes the
// profile file again (or keeps the file when the removal was not written yet: addSection takes it over).
// Nothing is restored over a profile that has the same name meanwhile. No toolkit.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "config.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

struct RemovedProfile {
    bool valid = false;
    Section::Kind kind = Section::DEVICE;
    std::string pattern;
    std::vector<KV> kv;   // every key of the section as it was (PRIORITY included)
    size_t place = 0;     // position among the profiles of its kind (0 = applied first)
};

// A copy of profile `section` (not Global); invalid for an index that does not exist.
inline RemovedProfile captureProfile(const SettingsModel &m, size_t section) {
    RemovedProfile r;
    const ProfileSet &ps = m.profiles();
    if (section == 0 || section >= ps.sections.size()) return r;
    const Section &s = ps.sections[section];
    r.valid = true;
    r.kind = s.kind;
    r.pattern = s.pattern;
    r.kv = s.kv;
    for (size_t i = 1; i < section; ++i)
        if (ps.sections[i].kind == s.kind) ++r.place;
    return r;
}

// Puts the profile back. true: it is in the model again (*newIndex = its section) and the autosave is due.
// false: nothing changed; *err says why (a profile of this name exists now, too many profiles).
inline bool restoreProfile(SettingsModel &m, const RemovedProfile &r, size_t *newIndex, std::string *err) {
    if (newIndex) *newIndex = ProfileSet::npos;
    if (!r.valid) {
        if (err) *err = "there is no removed profile to bring back";
        return false;
    }
    if (m.profiles().find(r.kind, r.pattern) != ProfileSet::npos) {
        if (err) *err = "a profile with this name exists again; the removed one was not restored";
        return false;
    }
    std::string e;
    size_t idx = m.addSection(r.kind, r.pattern, &e, false);
    if (idx == ProfileSet::npos) {
        if (err) *err = e;
        return false;
    }
    if (!m.setProfileKeys(idx, r.kv, &e)) {
        m.removeSection(idx, nullptr);
        if (err) *err = e;
        return false;
    }
    // The new profile is applied first; walk it to its old place (each step swaps with its neighbour).
    auto placeOf = [&m, &r](size_t i) {
        size_t p = 0;
        const ProfileSet &ps = m.profiles();
        for (size_t k = 1; k < i; ++k)
            if (ps.sections[k].kind == r.kind) ++p;
        return p;
    };
    for (size_t guard = 0; guard < 4096 && placeOf(idx) < r.place; ++guard) {
        size_t next = idx;
        if (!m.moveProfile(idx, false, &next, nullptr)) break;
        idx = next;
    }
    if (newIndex) *newIndex = idx;
    return true;
}

}  // namespace spn::ui
