// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Import of a profile file (the counterpart of profile_export.hpp). The header of the chosen .conf decides
// the kind: [device:NAME] becomes a device profile (devices/), [app:NAME] a program profile (apps/). The file is
// read and checked completely before anything changes (planProfileImport); only then it is applied
// (applyProfileImport) to the SettingsModel, whose autosave writes the new profile file. A profile with the same
// name (ASCII case-insensitive, same kind) is never overwritten unless the caller says Replace after asking the
// user once; Keep leaves the existing profile alone and says so. The PRIORITY of the file is not imported (the
// order belongs to this configuration): a new profile is applied last, a replaced one keeps its place. No toolkit.
#pragma once
#include <string>
#include <vector>

#include "config.hpp"
#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct ImportPlan {
    bool ok = false;
    std::string error;                 // why the file cannot be imported (translated)
    Section::Kind kind = Section::DEVICE;
    std::string name;                  // profile name from the header
    std::vector<KV> keys;              // the settings, PRIORITY removed
    bool collision = false;            // a profile of this kind and name exists already
    size_t existing = ProfileSet::npos;  // its section index
    std::vector<std::string> warnings;   // lines the parser skipped (they are not imported)
};

enum class ImportChoice { Replace, Keep };

struct ImportResult {
    bool done = false;                 // the model has the profile now (new or replaced)
    size_t section = ProfileSet::npos; // the profile's section (also for Keep: the existing one)
    std::string message;               // one line for the status bar (translated); also set on failure
};

inline ImportPlan planProfileImport(const SettingsModel &m, const std::string &file, const StringProvider &s) {
    ImportPlan p;
    std::string text;
    fs::ReadOptions opt;
    const fs::ReadResult rr = fs::readFileBounded(file, kMaxProfileFileBytes, opt, &text);
    if (!rr.ok()) {
        p.error = format(s.tr("%1 cannot be read as a profile file (not a regular file, too large or unreadable)."), {file});
        return p;
    }
    ProfileSet ps;
    parseProfilesText(text, file, &ps, &p.warnings);
    if (!ps.sections[0].kv.empty()) {
        p.error = s.tr("This file holds Global settings. Import takes a device or program profile ([device:...] or [app:...]).");
        return p;
    }
    if (ps.sections.size() < 2) {
        p.error = s.tr("This file holds no device or program profile ([device:...] or [app:...]).");
        return p;
    }
    if (ps.sections.size() > 2) {
        p.error = s.tr("This file holds several profiles; a profile file holds exactly one.");
        return p;
    }
    const Section &sec = ps.sections[1];
    p.kind = sec.kind;
    p.name = sec.pattern;
    Settings t;
    for (const KV &kv : sec.kv) {
        if (kv.first == "PRIORITY") continue;
        std::string e;
        if (!applyKey(t, kv.first, kv.second, &e)) {
            p.error = format(s.tr("The setting %1 = %2 is not valid: %3"), {kv.first, kv.second, e});
            return p;
        }
        p.keys.push_back(kv);
    }
    p.existing = m.profiles().find(p.kind, p.name);
    p.collision = p.existing != ProfileSet::npos;
    p.ok = true;
    return p;
}

// Applies a checked plan. Without a collision `choice` is ignored. The model is only changed on success.
inline ImportResult applyProfileImport(SettingsModel &m, const ImportPlan &p, ImportChoice choice, const StringProvider &s) {
    ImportResult r;
    if (!p.ok) {
        r.message = p.error;
        return r;
    }
    std::string err;
    if (p.collision) {
        r.section = p.existing;
        if (choice == ImportChoice::Keep) {
            r.message = format(s.tr("The existing profile \"%1\" was kept; the imported file was not used."), {p.name});
            return r;
        }
        if (!m.setProfileKeys(p.existing, p.keys, &err)) {
            r.message = format(s.tr("The profile \"%1\" could not be imported: %2"), {p.name, err});
            return r;
        }
        r.done = true;
        r.message = format(s.tr("The profile \"%1\" was replaced by the imported one."), {p.name});
        return r;
    }
    const size_t idx = m.addSection(p.kind, p.name, &err);
    if (idx == ProfileSet::npos) {
        r.message = format(s.tr("The profile \"%1\" could not be imported: %2"), {p.name, err});
        return r;
    }
    if (!m.setProfileKeys(idx, p.keys, &err)) {
        m.removeSection(idx, nullptr);
        r.message = format(s.tr("The profile \"%1\" could not be imported: %2"), {p.name, err});
        return r;
    }
    // The model sorts by priority and may have moved the new profile: find it again.
    r.section = m.profiles().find(p.kind, p.name);
    r.done = true;
    r.message = format(s.tr("Profile \"%1\" imported."), {p.name});
    return r;
}

}  // namespace spn::ui
