// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Per-user settings of the user interface itself (not of the daemon): the language (applied at the
// next start), the theme (applied at once) and the window state (size, position and last page,
// restored at the next start). Stored as "key = value" lines in
// <config base>/dofscroll/ui.conf, written atomically with user-only permissions through the
// file-system layer. No toolkit.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/fs.hpp"

namespace spn::ui {

enum class Language { System, English, German };

// Colour scheme: System keeps the platform look, Light and Dark force the dofscroll palette
// (src/ui/model/theme_colors.hpp).
enum class Theme { System, Light, Dark };

// Position and size of the settings window (outer frame, screen pixels). `valid` is false when
// nothing usable is stored.
struct WindowGeometry {
    bool valid = false;
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const WindowGeometry &o) const {
        return valid == o.valid && (!valid || (x == o.x && y == o.y && w == o.w && h == o.h));
    }
};

// A rectangle on the desktop (a screen's available area).
struct ScreenRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct UiSettings {
    Language language = Language::System;
    Theme theme = Theme::System;
    WindowGeometry window;  // last size and position of the window
    std::string page;       // registry id of the last selected page ("" = none)
    bool operator==(const UiSettings &o) const {
        return language == o.language && theme == o.theme && window == o.window && page == o.page;
    }
};

// Limits of what is accepted from the file (a hand-edited or damaged value cannot create a window
// of absurd size or place it far away).
constexpr int kMaxWindowSize = 20000;
constexpr int kMaxWindowCoord = 100000;
constexpr size_t kMaxPageIdLength = 48;

// "x,y,w,h" -> geometry. Exactly four decimal integers, w and h in 1..kMaxWindowSize, x and y within
// +-kMaxWindowCoord; anything else -> invalid. Blanks around the numbers are tolerated.
inline WindowGeometry parseWindowGeometry(const std::string &text) {
    long v[4];
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        const size_t end = i < 3 ? text.find(',', pos) : text.size();
        if (end == std::string::npos) return WindowGeometry();
        std::string field = text.substr(pos, end - pos);
        const size_t b = field.find_first_not_of(" \t");
        if (b == std::string::npos) return WindowGeometry();
        field = field.substr(b, field.find_last_not_of(" \t") - b + 1);
        if (field.size() > 8) return WindowGeometry();
        const size_t k = (field[0] == '-' || field[0] == '+') ? 1 : 0;
        if (k == field.size()) return WindowGeometry();
        for (size_t j = k; j < field.size(); ++j)
            if (field[j] < '0' || field[j] > '9') return WindowGeometry();
        v[i] = std::strtol(field.c_str(), nullptr, 10);
        pos = end + 1;
    }
    if (v[2] < 1 || v[2] > kMaxWindowSize || v[3] < 1 || v[3] > kMaxWindowSize) return WindowGeometry();
    if (v[0] < -kMaxWindowCoord || v[0] > kMaxWindowCoord || v[1] < -kMaxWindowCoord || v[1] > kMaxWindowCoord)
        return WindowGeometry();
    WindowGeometry g;
    g.valid = true;
    g.x = int(v[0]);
    g.y = int(v[1]);
    g.w = int(v[2]);
    g.h = int(v[3]);
    return g;
}

inline std::string windowGeometryText(const WindowGeometry &g) {
    return std::to_string(g.x) + "," + std::to_string(g.y) + "," + std::to_string(g.w) + "," + std::to_string(g.h);
}

// A page id is a short registry id: lower-case letters, digits, '-' and '_'.
inline bool validPageId(const std::string &id) {
    if (id.empty() || id.size() > kMaxPageIdLength) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

// The size raised to the smallest usable window.
inline WindowGeometry withMinimumSize(WindowGeometry g, int minW, int minH) {
    if (g.w < minW) g.w = minW;
    if (g.h < minH) g.h = minH;
    return g;
}

// Is enough of the window on a screen to grab and move it? The window must overlap one screen by at
// least `grab` pixels in both directions (less if the window itself is smaller). An empty screen
// list (no screen information) counts as visible, so that nothing is discarded without knowing.
inline bool geometryVisibleOn(const WindowGeometry &g, const std::vector<ScreenRect> &screens, int grab = 64) {
    if (!g.valid) return false;
    if (screens.empty()) return true;
    for (const ScreenRect &s : screens) {
        const long left = g.x > s.x ? g.x : s.x;
        const long top = g.y > s.y ? g.y : s.y;
        const long gr = long(g.x) + g.w, sr = long(s.x) + s.w, gb = long(g.y) + g.h, sb = long(s.y) + s.h;
        const long right = gr < sr ? gr : sr, bottom = gb < sb ? gb : sb;
        const long needW = grab < g.w ? grab : g.w, needH = grab < g.h ? grab : g.h;
        if (right - left >= needW && bottom - top >= needH) return true;
    }
    return false;
}

inline const char *themeId(Theme t) {
    switch (t) {
        case Theme::Light: return "light";
        case Theme::Dark: return "dark";
        default: return "system";
    }
}
// Unknown text -> System.
inline Theme themeFromId(const std::string &id) {
    if (id == "light") return Theme::Light;
    if (id == "dark") return Theme::Dark;
    return Theme::System;
}

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
        const std::string key = detail::trimWs(line.substr(0, eq)), value = detail::trimWs(line.substr(eq + 1));
        if (key == "language") s.language = languageFromId(value);
        else if (key == "theme") s.theme = themeFromId(value);
        else if (key == "window") s.window = parseWindowGeometry(value);
        else if (key == "page") s.page = validPageId(value) ? value : std::string();
    }
    return s;
}

inline std::string serializeUiSettings(const UiSettings &s) {
    std::string out = std::string("# dofscroll user interface settings\nlanguage = ") + languageId(s.language) +
                      "\ntheme = " + themeId(s.theme) + "\n";
    if (s.window.valid) out += "window = " + windowGeometryText(s.window) + "\n";
    if (validPageId(s.page)) out += "page = " + s.page + "\n";
    return out;
}

// Missing, unreadable, foreign-owned or oversized file: defaults. *status (optional) tells which
// case it was: NotFound is the normal first start, anything else other than Ok is a file that
// exists but cannot be used.
inline UiSettings loadUiSettings(const std::string &path, fs::ReadStatus *status = nullptr) {
    std::string text;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    const fs::ReadResult rr = fs::readFileBounded(path, 4096, opt, &text);
    if (status) *status = rr.status;
    if (!rr.ok()) return UiSettings();
    return parseUiSettings(text);
}

inline bool saveUiSettings(const std::string &path, const UiSettings &s, std::string *err) {
    return fs::writeFileReplace(path, serializeUiSettings(s), err);
}

// Changes one setting without touching the others: reads the file (defaults if it is missing),
// lets `change` modify the settings, writes them back. Used by every control of the interface that
// saves a setting, so that saving the language cannot reset the theme or the other way round.
// A file that exists but cannot be loaded (unreadable, foreign owner, oversized, not a regular
// file) is not replaced: the call fails with an error and the file stays as it is.
template <class Change>
inline bool updateUiSettings(const std::string &path, Change change, std::string *err) {
    fs::ReadStatus st = fs::ReadStatus::Ok;
    UiSettings s = loadUiSettings(path, &st);
    if (st != fs::ReadStatus::Ok && st != fs::ReadStatus::NotFound) {
        if (err) *err = "the existing settings file " + path + " cannot be read, it was not changed";
        return false;
    }
    change(s);
    return saveUiSettings(path, s, err);
}

}  // namespace spn::ui
