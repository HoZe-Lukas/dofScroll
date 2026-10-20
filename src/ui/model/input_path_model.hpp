// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Model of the "Input path" setting (config key INPUT) and of the About text on the Settings page.
// Toolkit-free; the view passes a StringProvider and shows what these functions return. The control
// exists on Windows and macOS only (on Linux spacenavd is the driver and INPUT is ignored).
#pragma once
#include <string>
#include <vector>

#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct InputChoice {
    std::string value;  // the INPUT value written to the configuration
    std::string label;  // shown in the drop-down
};

// Order of the drop-down; the first entry is the default.
inline std::vector<InputChoice> inputChoices(const StringProvider &s) {
    return {{"auto", s.tr("Automatic")}, {"driver", s.tr("3Dconnexion driver")}, {"hid", s.tr("Direct (HID)")}};
}

// Index of a configuration value in inputChoices(); unknown values show as Automatic (what the daemon does).
inline int inputChoiceIndex(const std::string &value) {
    return value == "driver" ? 1 : value == "hid" ? 2 : 0;
}

// The control is shown on Windows and macOS only.
inline bool inputChoiceAvailable(bool windowsOrMac) { return windowsOrMac; }

inline std::string inputExplanation(const StringProvider &s) {
    return s.tr("How dofscroll reads the SpaceMouse. Automatic: through the interface of the official 3Dconnexion driver "
                "when the driver is installed, otherwise directly. 3Dconnexion driver: always through the installed "
                "driver. Direct (HID): always directly, without the driver. The driver itself keeps running for your "
                "CAD programs in every case. A change takes effect within a few seconds.");
}

// "Active input: 3Dconnexion driver" / "Active input: direct (HID)" / "Active input: not known (the daemon reports none)".
inline std::string inputActiveLine(const StatusModel &m, const StringProvider &s) {
    const std::string in = m.inputLine(s);
    if (in.empty()) return s.tr("Active input: not known yet");
    const std::string &id = m.status().input;
    return format(s.tr("Active input: %1"), {id == "driver" ? s.tr("3Dconnexion driver") : s.tr("direct (HID)")});
}

// One note per line for the About section. The driver note is shown on Windows and macOS only.
inline std::vector<std::string> aboutLines(const std::string &version, const std::string &qtVersion, bool windowsOrMac,
                                           const StringProvider &s) {
    std::vector<std::string> v;
    v.push_back(format(s.tr("dofscroll %1"), {version}));
    v.push_back(s.tr("dofscroll is free software under the GPL-3.0-or-later."));
    v.push_back(format(s.tr("This program uses Qt %1 under the LGPL (https://www.qt.io)."), {qtVersion}));
    if (windowsOrMac)
        v.push_back(s.tr("The official 3Dconnexion driver, its interface and the names 3Dconnexion and SpaceMouse belong to "
                         "3Dconnexion (a Logitech company). dofscroll uses the driver's interface when the driver is "
                         "installed and does not contain any of it. Not affiliated with or endorsed by 3Dconnexion."));
    return v;
}

}  // namespace spn::ui
