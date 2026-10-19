// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Per-user settings of the user interface itself (not of the daemon): today only the language.
// Stored as "key = value" lines in <config base>/dofscroll/ui.conf, written atomically with
// user-only permissions through the file-system layer. Applied at the next start of dofscroll-ui.
// No toolkit.
#pragma once
#include <cstddef>
#include <string>

#include "platform/fs.hpp"

namespace spn::ui {

enum class Language { System, English, German };

struct UiSettings {
    Language language = Language::System;
    bool operator==(const UiSettings &o) const { return language == o.language; }
};

inline const char *languageId(Language l) {
    switch (l) {
        case Language::English: return "en";
        case Language::German: return "de";
        default: return "system";
    }
}
// Unknown text -> System.
inline Language languageFromId(const std::string &id) {
    if (id == "en") return Language::English;
    if (id == "de") return Language::German;
    return Language::System;
}

// Locale name for the translation lookup ("" = use the system locale, "en" = no translation).
inline std::string languageLocaleName(Language l) { return l == Language::System ? "" : languageId(l); }

inline std::string uiSettingsPath() { return fs::userConfigBase() + "/dofscroll/ui.conf"; }

namespace detail {
inline std::string trimWs(const std::string &v) {
    const char *ws = " \t\r";
    const size_t b = v.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    return v.substr(b, v.find_last_not_of(ws) - b + 1);
}
}  // namespace detail

// Tolerant: unknown keys, comments and bad lines are ignored, the last value of a key wins.
inline UiSettings parseUiSettings(const std::string &text) {
    UiSettings s;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line =
            detail::trimWs(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (detail::trimWs(line.substr(0, eq)) == "language")
            s.language = languageFromId(detail::trimWs(line.substr(eq + 1)));
    }
    return s;
}

inline std::string serializeUiSettings(const UiSettings &s) {
    return std::string("# dofscroll user interface settings\nlanguage = ") + languageId(s.language) + "\n";
}

// Missing, unreadable, foreign-owned or oversized file: defaults.
inline UiSettings loadUiSettings(const std::string &path) {
    std::string text;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    if (!fs::readFileBounded(path, 4096, opt, &text).ok()) return UiSettings();
    return parseUiSettings(text);
}

inline bool saveUiSettings(const std::string &path, const UiSettings &s, std::string *err) {
    return fs::writeFileReplace(path, serializeUiSettings(s), err);
}

}  // namespace spn::ui
