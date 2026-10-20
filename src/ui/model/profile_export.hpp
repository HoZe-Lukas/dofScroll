// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Export of one profile: the text of a .conf file that holds exactly that profile's section
// ([global], [device:NAME] or [app:NAME]) with the keys the profile sets itself. The file can be
// read like any dofscroll.conf (loadProfiles) or copied into another configuration. No toolkit.
#pragma once
#include <string>

#include "ui/model/settings_model.hpp"

namespace spn::ui {

// Text of the export file of profile `section`; empty if there is no such profile. A line break
// in the keys or values cannot occur (SettingsModel validates them).
inline std::string profileExportText(const SettingsModel &m, size_t section) {
    const ProfileSet &ps = m.profiles();
    if (section >= ps.sections.size()) return std::string();
    const Section &sec = ps.sections[section];
    std::string out = "# dofscroll profile, exported from the settings window.\n"
                      "# Copy the section below into dofscroll.conf (comments are not kept when the program saves).\n\n";
    if (sec.kind == Section::DEVICE) out += "[device:" + sec.pattern + "]\n";
    else if (sec.kind == Section::APP) out += "[app:" + sec.pattern + "]\n";
    else out += "[global]\n";
    for (const KV &kv : sec.kv) out += kv.first + " = " + kv.second + "\n";
    return out;
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

}  // namespace spn::ui
