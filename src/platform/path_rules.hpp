// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure path rules of the three OS families (no OS calls), so they are unit-tested on every
// platform: where the configuration and runtime directories live. The OS backends feed them
// with the real environment.
#pragma once
#include <cctype>
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

// ---- Windows path hazards ---------------------------------------------------------------------
// Names and forms that Windows resolves to something other than the file the string seems to name.
// The Windows file-system layer rejects them for every path it receives (the pure check lives here
// so it is unit-tested on all OSes); mode ids use isReservedDeviceStem on all OSes so a mode file
// "con.conf" can never exist in a directory that is copied to Windows.

enum class PathHazard {
    None,
    Empty,
    InvalidChar,         // control characters and < > " | ? *
    ReservedName,        // CON, PRN, AUX, NUL, COM1-9, LPT1-9, CONIN$, CONOUT$ (also with an extension)
    AlternateStream,     // "file:stream" (a ':' anywhere except right after the drive letter)
    TrailingDotOrSpace,  // "name." / "name " (Win32 strips them: another file is opened)
    VerbatimPrefix,      // \\?\ (no normalization, no length or name checks)
    DevicePrefix,        // \\.\ and \??\ (device namespace)
    DriveRelative,       // "C:" and "C:name" (relative to the per-drive current directory)
};

inline const char *pathHazardName(PathHazard h) {
    switch (h) {
    case PathHazard::None: return "none";
    case PathHazard::Empty: return "empty path";
    case PathHazard::InvalidChar: return "invalid character";
    case PathHazard::ReservedName: return "reserved device name";
    case PathHazard::AlternateStream: return "alternate data stream";
    case PathHazard::TrailingDotOrSpace: return "trailing dot or space";
    case PathHazard::VerbatimPrefix: return "\\\\?\\ prefix";
    case PathHazard::DevicePrefix: return "device namespace prefix";
    case PathHazard::DriveRelative: return "drive-relative path";
    }
    return "unknown";
}

// True for the DOS device names Windows resolves regardless of directory and extension. `stem`
// is the part of a file name before the first '.'; case-insensitive, trailing spaces ignored.
// COM and LPT followed by the superscript digits 1-3 (UTF-8) are reserved as well (Windows 10).
inline bool isReservedDeviceStem(const std::string &stemIn) {
    std::string s = stemIn;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "con" || s == "prn" || s == "aux" || s == "nul" || s == "conin$" || s == "conout$") return true;
    if (s.size() == 4 && (s.compare(0, 3, "com") == 0 || s.compare(0, 3, "lpt") == 0) && s[3] >= '1' && s[3] <= '9')
        return true;
    if (s.size() == 5 && (s.compare(0, 3, "com") == 0 || s.compare(0, 3, "lpt") == 0)) {
        const unsigned char a = static_cast<unsigned char>(s[3]), b = static_cast<unsigned char>(s[4]);
        if (a == 0xC2 && (b == 0xB9 || b == 0xB2 || b == 0xB3)) return true;  // U+00B9, U+00B2, U+00B3
    }
    return false;
}

// A single path component (no separators): "con", "CON.txt", "nul .x" are reserved.
inline bool isReservedDeviceName(const std::string &component) {
    return isReservedDeviceStem(component.substr(0, component.find('.')));
}

// First hazard found in a path given with '/' or '\\' separators ("C:/dir/file", "//server/share/x",
// "relative/x"). "." and ".." components are allowed. A normal UNC path is allowed.
inline PathHazard windowsPathHazard(const std::string &path) {
    if (path.empty()) return PathHazard::Empty;
    const std::string p = toSlashes(path);
    if (p.compare(0, 4, "//?/") == 0) return PathHazard::VerbatimPrefix;
    if (p.compare(0, 4, "//./") == 0 || p.compare(0, 4, "/?" "?/") == 0  /* NT namespace; split so it is no trigraph */) return PathHazard::DevicePrefix;
    size_t i = 0;
    if (p.size() >= 2 && p[1] == ':') {
        const char d = p[0];
        if (!((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z'))) return PathHazard::AlternateStream;
        if (p.size() == 2 || p[2] != '/') return PathHazard::DriveRelative;
        i = 2;
    }
    for (char c : p)
        if (static_cast<unsigned char>(c) < 0x20 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return PathHazard::InvalidChar;
    if (p.find(':', i) != std::string::npos) return PathHazard::AlternateStream;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string comp = p.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == "." || comp == "..") continue;
        if (comp.back() == '.' || comp.back() == ' ') return PathHazard::TrailingDotOrSpace;
        if (isReservedDeviceName(comp)) return PathHazard::ReservedName;
    }
    return PathHazard::None;
}

// Converts the verbatim form GetFinalPathNameByHandle returns to the normal form:
// "\\?\C:\x" -> "C:\x", "\\?\UNC\server\share\x" -> "\\server\share\x". Other input is unchanged.
inline std::string stripVerbatimPrefix(const std::string &w) {
    if (w.compare(0, 8, "\\\\?\\UNC\\") == 0) return "\\\\" + w.substr(8);
    if (w.compare(0, 4, "\\\\?\\") == 0) return w.substr(4);
    return w;
}

}  // namespace spn
