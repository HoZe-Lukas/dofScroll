// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Export of one profile: the text of a .conf file that holds exactly that profile's section
// ([global], [device:NAME] or [app:NAME]). A device or program profile that has a file of its own
// (devices/<slug>.conf, apps/<slug>.conf) and no edits waiting is exported as a copy of that file, byte for
// byte; every other profile (Global, a profile of an old single file, one with edits not yet written) is
// rendered in the same format. The result is a valid profile file: Import (profile_import.hpp) takes it, and
// it can be read like any dofscroll.conf (loadProfiles). No toolkit.
#pragma once
#include <string>

#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

// Text of the export file of profile `section`; empty if there is no such profile. A line break
// in the keys or values cannot occur (SettingsModel validates them).
inline std::string profileExportText(const SettingsModel &m, size_t section) {
    const ProfileSet &ps = m.profiles();
    if (section >= ps.sections.size()) return std::string();
    const Section &sec = ps.sections[section];
    const std::string file = m.profileFile(section);
    if (!file.empty() && !m.profileDirty(section)) {  // the profile file itself
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        std::string text;
        if (fs::readFileBounded(file, kMaxProfileFileBytes, opt, &text).ok() && !text.empty()) return text;
    }
    if (section == 0) {
        std::string out = "# dofscroll Global profile, exported from the settings window.\n\n[global]\n";
        for (const KV &kv : sec.kv) out += kv.first + " = " + kv.second + "\n";
        return out;
    }
    std::string body, err;
    if (!renderProfileSection(sec, &body, &err)) return std::string();
    return "# dofscroll profile, exported from the settings window.\n"
           "# Import it again with Import profile, or put it into devices/ or apps/ of a configuration directory.\n\n" +
           body;
}

// A file name for the export, from the profile name ("firefox" -> "firefox.conf"; characters that
// are not safe in a file name become '_').
inline std::string profileExportFileName(const SettingsModel &m, size_t section) {
    const ProfileSet &ps = m.profiles();
    if (section >= ps.sections.size()) return "profile.conf";
    std::string base = section == 0 ? "global" : ps.sections[section].pattern;
    for (char &c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    if (base.empty() || base == "." || base == "..") base = "profile";
    if (base.size() > 80) base.resize(80);
    return base + ".conf";
}

// Writes the export file (atomic replace). false + *err on failure.
inline bool exportProfileTo(const SettingsModel &m, size_t section, const std::string &dest, std::string *err) {
    const std::string text = profileExportText(m, section);
    if (text.empty()) {
        if (err) *err = "No such profile.";
        return false;
    }
    return fs::writeFileReplace(dest, text, err);
}

}  // namespace spn::ui
