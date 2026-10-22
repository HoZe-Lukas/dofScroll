// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the user has already done in the interface, for the "Getting started" list of the Overview:
// whether the axis assistant was applied once and whether the guided tour of the joystick mode editor was
// shown (done or skipped). Kept in its own small file next to
// the configuration (<config dir>/ui-progress.conf, "key = value" lines, written atomically with
// user-only permissions), apart from the interface settings of ui_settings.hpp. No toolkit.
#pragma once
#include <string>

#include "platform/fs.hpp"

namespace spn::ui {

struct UiProgress {
    bool axisAssistantApplied = false;
    bool motionTourSeen = false;  // the tour of the Motions and buttons tab ran to its end or was dismissed
    bool operator==(const UiProgress &o) const {
        return axisAssistantApplied == o.axisAssistantApplied && motionTourSeen == o.motionTourSeen;
    }
};

// Same directory as the configuration file.
inline std::string uiProgressPath(const std::string &configPath) {
    const size_t sep = fs::lastSeparator(configPath);
    return (sep == std::string::npos ? std::string() : configPath.substr(0, sep + 1)) + "ui-progress.conf";
}

inline UiProgress parseUiProgress(const std::string &text) {
    auto trim = [](const std::string &v) {
        const char *ws = " \t\r";
        const size_t b = v.find_first_not_of(ws);
        return b == std::string::npos ? std::string() : v.substr(b, v.find_last_not_of(ws) - b + 1);
    };
    UiProgress p;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "axis_assistant_applied") p.axisAssistantApplied = value == "1";
        if (key == "motion_tour_seen") p.motionTourSeen = value == "1";
    }
    return p;
}

inline std::string serializeUiProgress(const UiProgress &p) {
    return std::string("# dofscroll user interface progress\naxis_assistant_applied = ") +
           (p.axisAssistantApplied ? "1" : "0") + "\nmotion_tour_seen = " + (p.motionTourSeen ? "1" : "0") + "\n";
}

// Missing, unreadable, foreign-owned or oversized file: nothing done yet.
inline UiProgress loadUiProgress(const std::string &path) {
    std::string text;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    if (!fs::readFileBounded(path, 4096, opt, &text).ok()) return UiProgress();
    return parseUiProgress(text);
}

inline bool saveUiProgress(const std::string &path, const UiProgress &p, std::string *err) {
    return fs::writeFileReplace(path, serializeUiProgress(p), err);
}

// The axis assistant wrote its result into the configuration.
inline bool markAxisAssistantApplied(const std::string &path, std::string *err) {
    UiProgress p = loadUiProgress(path);
    if (p.axisAssistantApplied) return true;
    p.axisAssistantApplied = true;
    return saveUiProgress(path, p, err);
}

// The guided tour of the Motions and buttons tab was finished or dismissed: it is not shown again.
inline bool markMotionTourSeen(const std::string &path, std::string *err) {
    UiProgress p = loadUiProgress(path);
    if (p.motionTourSeen) return true;
    p.motionTourSeen = true;
    return saveUiProgress(path, p, err);
}

}  // namespace spn::ui
