// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Entries of the "Profiles" tree: "Global" ([global]) first, then the device profiles and
// then the application profiles, each group in the order the daemon applies them (PRIORITY ascending, ties in the
// order the model holds them), then the defective profile files of the group (own entries with the file name, never overwritten).
// Add/remove/rename/move go through the SettingsModel; the section index is the identity of an entry until
// the next rebuild.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "profile_match.hpp"
#include "ui/model/app_picker_model.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

struct ProfileEntry {
    ProfileEntry() = default;
    ProfileEntry(size_t sec, Section::Kind k, std::string pat, bool def)
        : section(sec), kind(k), pattern(std::move(pat)), isDefault(def) {}
    size_t section = 0;      // index in the SettingsModel
    Section::Kind kind = Section::GLOBAL;
    std::string pattern;     // empty for the default mode
    bool isDefault = false;  // [global]: no mode selector, cannot be removed or renamed
    int priority = 0;        // PRIORITY (the order inside the group)
    // A profile file that is not loaded: `section` is meaningless (npos), `file` is its name and `message` the reason.
    bool defective = false;
    bool directory = false;  // a whole folder that cannot be read (file = its name)
    bool duplicate = false;  // defective because another file holds the same profile
    bool legacyActive = false;  // the profile of this name is still read from dofscroll.conf
    std::string file;        // base name of the defective file
    std::string message;
};

class ProfileTreeModel {
public:
    static std::vector<ProfileEntry> entries(const SettingsModel &m) {
        std::vector<ProfileEntry> dev, app, out;
        out.push_back({0, Section::GLOBAL, "", true});
        for (const SettingsModel::SectionInfo &s : m.sections()) {
            if (s.index == 0) continue;
            ProfileEntry e{s.index, s.kind, s.pattern, false};
            e.priority = m.priority(s.index);
            (s.kind == Section::DEVICE ? dev : app).push_back(e);
        }
        // The model keeps the profiles of a kind in applied order already (PRIORITY, ties in load order); the list shows
        // exactly that order, so Move up / Move down and "applied after ..." match what is on screen.
        auto byOrder = [](const ProfileEntry &a, const ProfileEntry &b) { return a.priority < b.priority; };
        std::stable_sort(dev.begin(), dev.end(), byOrder);
        std::stable_sort(app.begin(), app.end(), byOrder);
        for (const SettingsModel::Defect &d : m.defects()) {
            ProfileEntry e{ProfileSet::npos, d.kind, d.name, false};
            e.defective = true;
            e.duplicate = d.duplicate;
            e.directory = d.directory;
            e.legacyActive = d.legacyActive;
            const size_t k = d.file.find_last_of('/');
            e.file = k == std::string::npos ? d.file : d.file.substr(k + 1);
            e.message = d.message;
            (d.kind == Section::DEVICE ? dev : app).push_back(e);
        }
        out.insert(out.end(), dev.begin(), dev.end());
        out.insert(out.end(), app.begin(), app.end());
        return out;
    }
    // The text of a defective entry in the list: the file name and that it is defective (English source; the view
    // translates the parts).
    static std::string defectLabel(const ProfileEntry &e) { return e.file; }
    // Delete a profile: its file is removed by the next save (after checking it is unchanged), nothing is asked
    // about unsaved changes. Rename: the new file is written first, then the old one removed.
    static bool move(SettingsModel &m, size_t section, bool up, size_t *newIndex, std::string *err) {
        return m.moveProfile(section, up, newIndex, err);
    }

    // Search box of the profile list: the entries whose name contains the text (ASCII
    // case-insensitive, see docs/encoding.md). The default mode is always kept. Empty text keeps all.
    static std::vector<ProfileEntry> filter(const std::vector<ProfileEntry> &in, const std::string &text) {
        std::vector<ProfileEntry> out;
        for (const ProfileEntry &e : in)
            if (e.isDefault || filterMatches(text, e.pattern, e.file)) out.push_back(e);
        return out;
    }
    // Does a search text match any of the given strings (labels, keys)? An empty text matches.
    static bool filterMatches(const std::string &text, const std::string &a, const std::string &b = std::string()) {
        const std::string n = lowerAscii(text);
        if (n.empty()) return true;
        return lowerAscii(a).find(n) != std::string::npos || lowerAscii(b).find(n) != std::string::npos;
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
    // A rename can move the profile in the list (ties of PRIORITY are broken by file name): *newSection is where it
    // is afterwards.
    static bool rename(SettingsModel &m, size_t section, const std::string &typed, std::string *err,
                       size_t *newSection = nullptr) {
        std::string norm, e;
        if (!validateProfilePattern(typed, &norm, &e)) {
            if (err) *err = e;
            return false;
        }
        const Section::Kind kind = section < m.sections().size() ? m.sections()[section].kind : Section::GLOBAL;
        if (!m.renameSection(section, norm, err)) return false;
        if (newSection) *newSection = resolve(m, kind, norm);
        return true;
    }
    // The index of the profile with this identity now (the model may have been reloaded or re-sorted since an
    // index was taken), npos when it is gone. Page code that opens a dialog between taking an index and using it
    // must go through this.
    static size_t resolve(const SettingsModel &m, Section::Kind kind, const std::string &pattern) {
        return kind == Section::GLOBAL ? 0 : m.profiles().find(kind, pattern);
    }

    // English source text of the tree label (the view translates "Global").
    static std::string label(const ProfileEntry &e) { return e.isDefault ? "Global" : e.pattern; }
    static const char *groupTitle(Section::Kind k) {
        return k == Section::DEVICE ? "Device profiles" : k == Section::APP ? "Application profiles" : "";
    }
};

// What the profile page shows for a section; rebuilt when the effective mode changes.
struct ProfilePageLayout {
    bool modeSelector = false;      // device/app profiles only
    bool bindingEditor = true;      // always: the buttons of a profile are editable in every joystick mode
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
    l.bindingEditor = true;  // buttons and motions are always editable in a profile; a joystick mode that binds a button is said in words
    l.modeInfo = !v.isDefaultMode && v.modeLoaded;
    l.editModeFile = l.modeInfo && v.mode.hasFile;
    return l;
}

}  // namespace spn::ui
