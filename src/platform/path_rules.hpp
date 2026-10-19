// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure path rules of the three OS families (no OS calls), so they are unit-tested on every
// platform: where the configuration and runtime directories live. The OS backends feed them
// with the real environment.
#pragma once
#include <string>

namespace spn {

// Converts '\\' to '/' (the core joins paths with '/'; Windows accepts both).
inline std::string toSlashes(std::string p) {
    for (char &c : p)
        if (c == '\\') c = '/';
    return p;
}

inline std::string stripTrailingSlashes(std::string p) {
    while (p.size() > 1 && p.back() == '/' && !(p.size() == 3 && p[1] == ':')) p.pop_back();
    return p;
}

// Absolute for the purposes of the rules below: "/x", "C:/x", "//server/share".
inline bool isAbsolutePath(const std::string &p) {
    if (p.empty()) return false;
    if (p[0] == '/') return true;
    return p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' &&
           (p[2] == '/' || p[2] == '\\');
}

// Windows: %APPDATA% (roaming). Empty `appdata` falls back to <userprofile>/AppData/Roaming.
inline std::string windowsConfigBase(const std::string &appdata, const std::string &userprofile) {
    const std::string a = toSlashes(appdata);
    if (isAbsolutePath(a)) return stripTrailingSlashes(a);
    const std::string u = toSlashes(userprofile);
    if (isAbsolutePath(u)) return stripTrailingSlashes(u) + "/AppData/Roaming";
    return "./AppData/Roaming";
}
// Windows: %LOCALAPPDATA% (not roamed: status, log, suspend files).
inline std::string windowsRuntimeBase(const std::string &localAppData, const std::string &userprofile) {
    const std::string a = toSlashes(localAppData);
    if (isAbsolutePath(a)) return stripTrailingSlashes(a);
    const std::string u = toSlashes(userprofile);
    if (isAbsolutePath(u)) return stripTrailingSlashes(u) + "/AppData/Local";
    return "";
}

// macOS: ~/Library/Application Support and ~/Library/Caches. Empty/relative home -> no path.
inline std::string macConfigBase(const std::string &home) {
    if (!isAbsolutePath(home)) return "./Library/Application Support";
    return stripTrailingSlashes(home) + "/Library/Application Support";
}
inline std::string macRuntimeBase(const std::string &home) {
    if (!isAbsolutePath(home)) return "";
    return stripTrailingSlashes(home) + "/Library/Caches";
}

}  // namespace spn
