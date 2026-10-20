// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the user has already done in the interface, for the "Getting started" list of the Overview:
// at the moment whether the axis assistant was applied once. Kept in its own small file next to
// the configuration (<config dir>/ui-progress.conf, "key = value" lines, written atomically with
// user-only permissions), apart from the interface settings of ui_settings.hpp. No toolkit.
#pragma once
#include <string>

#include "platform/fs.hpp"

namespace spn::ui {

struct UiProgress {
    bool axisAssistantApplied = false;
    bool operator==(const UiProgress &o) const { return axisAssistantApplied == o.axisAssistantApplied; }
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
        if (trim(line.substr(0, eq)) == "axis_assistant_applied") p.axisAssistantApplied = trim(line.substr(eq + 1)) == "1";
    }
    return p;
}

inline std::string serializeUiProgress(const UiProgress &p) {
    return std::string("# dofscroll user interface progress\naxis_assistant_applied = ") +
           (p.axisAssistantApplied ? "1" : "0") + "\n";
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

}  // namespace spn::ui
