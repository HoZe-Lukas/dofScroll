// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Presentation logic of the "General" page: what the start-at-login checkbox shows for each
// autostart state, and the lines of the read-only status block. No toolkit.
#pragma once
#include <string>
#include <vector>

#include "platform/autostart_platform.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct AutostartView {
    bool checked = false;
    bool partial = false;     // show the half-checked state
    bool canToggle = false;   // checkbox usable
    bool isError = false;     // text is an error/unavailable message
    std::string text;         // message below the checkbox ("" = none)
    std::string tooltip;
};

// detail: the text delivered by queryAutostart (untrusted OS text, shown as is, length limited).
inline AutostartView autostartView(autostart::AutostartState s, const std::string &detail,
                                   const StringProvider &str) {
    using autostart::AutostartState;
    AutostartView v;
    std::string d = detail.size() > 200 ? detail.substr(0, 200) : detail;
    for (char &c : d)
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    switch (s) {
        case AutostartState::Enabled:
            v.checked = true;
            v.canToggle = true;
            v.tooltip = str.tr("dofscroll starts automatically at the next login");
            break;
        case AutostartState::Disabled:
            v.canToggle = true;
            v.tooltip = str.tr("dofscroll does not start at login");
            break;
        case AutostartState::Partial:
            v.partial = true;
            v.canToggle = true;
            v.text = str.tr("Only part of the start-at-login setup is active. Ticking the box repairs it.");
            if (!d.empty()) v.text += " (" + d + ")";
            v.tooltip = v.text;
            break;
        case AutostartState::Unavailable:
            v.isError = true;
            v.text = str.tr("Start at login cannot be changed here");
            if (!d.empty()) v.text += ": " + d;
            v.tooltip = v.text;
            break;
        case AutostartState::Error:
        default:
            v.isError = true;
            v.text = str.tr("Could not read the start-at-login state");
            if (!d.empty()) v.text += ": " + d;
            v.tooltip = v.text;
            break;
    }
    return v;
}

// Status block lines (all already sanitized by StatusModel). Empty strings are left out.
inline std::vector<std::string> statusBlockLines(const StatusModel &m, const StringProvider &str,
                                                 const std::string &modeLabel = std::string()) {
    std::vector<std::string> out;
    out.push_back(m.daemonRunning() ? str.tr("Daemon: running") : str.tr("Daemon: not running"));
    if (m.daemonRunning()) {
        out.push_back(m.mainLine(str, modeLabel));
        const std::string info = m.infoLine(str);
        if (!info.empty()) out.push_back(info);
        const std::string w = m.warningLine(str);
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

}  // namespace spn::ui
