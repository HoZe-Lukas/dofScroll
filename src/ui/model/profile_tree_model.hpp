// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Entries of the "Profiles" tree: "Default mode" ([global]) first, then the device profiles and
// then the application profiles, each group sorted by name. Add/remove/rename go through the
// SettingsModel; the section index is the identity of an entry until the next rebuild.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "profile_match.hpp"
#include "ui/model/app_picker_model.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

struct ProfileEntry {
    size_t section = 0;      // index in the SettingsModel
    Section::Kind kind = Section::GLOBAL;
    std::string pattern;     // empty for the default mode
    bool isDefault = false;  // [global]: no mode selector, cannot be removed or renamed
};

class ProfileTreeModel {
public:
    static std::vector<ProfileEntry> entries(const SettingsModel &m) {
        std::vector<ProfileEntry> dev, app, out;
        out.push_back({0, Section::GLOBAL, "", true});
        for (const SettingsModel::SectionInfo &s : m.sections()) {
            if (s.index == 0) continue;
            (s.kind == Section::DEVICE ? dev : app).push_back({s.index, s.kind, s.pattern, false});
        }
        auto byName = [](const ProfileEntry &a, const ProfileEntry &b) {
            return lowerAscii(a.pattern) < lowerAscii(b.pattern);
        };
        std::stable_sort(dev.begin(), dev.end(), byName);
        std::stable_sort(app.begin(), app.end(), byName);
        out.insert(out.end(), dev.begin(), dev.end());
        out.insert(out.end(), app.begin(), app.end());
        return out;
    }

    // Patterns of the existing profiles of a kind (input of the pickers' "already has a profile").
    static std::vector<std::string> patterns(const SettingsModel &m, Section::Kind kind) {
        std::vector<std::string> out;
        for (const SettingsModel::SectionInfo &s : m.sections())
            if (s.index != 0 && s.kind == kind) out.push_back(s.pattern);
        return out;
    }

    // Adds a profile (or returns the existing one). npos + *err on failure.
    static size_t add(SettingsModel &m, Section::Kind kind, const std::string &typed, std::string *err) {
        std::string norm, e;
        if (!validateProfilePattern(typed, &norm, &e)) {
            if (err) *err = e;
            return ProfileSet::npos;
        }
        return m.addSection(kind, norm, err);
    }
    static bool remove(SettingsModel &m, size_t section, std::string *err) {
        return m.removeSection(section, err);
    }
    static bool rename(SettingsModel &m, size_t section, const std::string &typed, std::string *err) {
        std::string norm, e;
        if (!validateProfilePattern(typed, &norm, &e)) {
            if (err) *err = e;
            return false;
        }
        return m.renameSection(section, norm, err);
    }

    // English source text of the tree label (the view translates "Default mode").
    static std::string label(const ProfileEntry &e) { return e.isDefault ? "Default mode" : e.pattern; }
    static const char *groupTitle(Section::Kind k) {
        return k == Section::DEVICE ? "Device profiles" : k == Section::APP ? "Application profiles" : "";
    }
};

// What the profile page shows for a section; rebuilt when the effective mode changes.
struct ProfilePageLayout {
    bool modeSelector = false;      // device/app profiles only
    bool bindingEditor = false;     // default mode (or an unknown mode id): editable bindings + NAV/CLICK
    bool modeInfo = false;          // another loaded mode: read-only channel actions and bindings
    bool editModeFile = false;      // "Edit mode file" button (the mode has a file)
    bool unknownMode = false;       // MODE names no loaded mode (treated like the default mode)
    bool overridesSection = false;  // collapsed and off by default
    std::string modeId;
};

inline ProfilePageLayout profilePageLayout(const SettingsModel &m, size_t section) {
    ProfilePageLayout l;
    const SettingsModel::Visible v = m.visibleSettings(section);
    l.modeId = v.modeId;
    l.modeSelector = v.canChooseMode;
    l.overridesSection = section > 0;
    l.unknownMode = !v.isDefaultMode && !v.modeLoaded;
    l.bindingEditor = v.isDefaultMode || !v.modeLoaded;
    l.modeInfo = !v.isDefaultMode && v.modeLoaded;
    l.editModeFile = l.modeInfo && v.mode.hasFile;
    return l;
}

}  // namespace spn::ui
