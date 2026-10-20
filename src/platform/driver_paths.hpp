// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Where the driver interface may be loaded from, as pure rules (no OS calls; tests/test_driver_paths.cpp
// runs them on every platform). The daemon only loads the library of the installed official driver
// from FIXED locations and only after these rules accepted the path: no environment variable, config
// key or command-line option can redirect it (that would be a code-injection hole; docs/security.md,
// "Driver interface"). Tests load a fake driver through an explicit path parameter of the loader, never
// through this table.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spn {

// macOS: the client framework of the installed 3Dconnexion driver. The path is the one public
// open-source programs use (Blender, intern/ghost/intern/GHOST_NDOFManagerCocoa.mm).
constexpr const char *kMacClientFrameworkDir = "/Library/Frameworks/3DconnexionClient.framework";
constexpr const char *kMacClientLibrary = "/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient";

// Windows: the ProgID of the COM object of the installed driver (public documentation and forum
// samples: "TDxInput.Device"). Only the registry below HKEY_LOCAL_MACHINE (admin-writable) is consulted
// for its server DLL, never HKEY_CURRENT_USER, which a non-admin program could plant.
constexpr const char *kWinDriverProgId = "TDxInput.Device";

// "C:\Program Files\x\y.dll" and "c:/program files/x/y.dll" compare equal; the result uses '/'.
// Returns "" for anything that is not an absolute drive path (relative, UNC, device paths "\\?\" and
// "\\.\", empty) or that contains a ".." or "." component, an ASCII control character or a ':' beyond
// the drive letter (alternate data streams).
inline std::string normalizeWinDriverPath(const std::string &in) {
    if (in.size() < 4 || in.size() > 1024) return "";
    std::string p;
    p.reserve(in.size());
    for (char c : in) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) return "";
        p.push_back(c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    const bool drive = p[0] >= 'a' && p[0] <= 'z' && p[1] == ':' && p[2] == '/';
    if (!drive) return "";
    if (p.find(':', 2) != std::string::npos) return "";
    // components
    size_t i = 3;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string comp = p.substr(i, j - i);
        if (comp == "." || comp == "..") return "";
        if (comp.empty() && j != p.size()) return "";  // "//" inside the path
        i = j + 1;
    }
    return p;
}

// True when `path` lies strictly below one of `roots` (e.g. the Program Files folders as the OS
// reports them). `roots` are normalized the same way; an empty or invalid root never matches.
inline bool winDriverPathAllowed(const std::string &path, const std::vector<std::string> &roots) {
    const std::string p = normalizeWinDriverPath(path);
    if (p.empty()) return false;
    for (const std::string &r : roots) {
        std::string n = normalizeWinDriverPath(r);
        if (n.empty()) continue;
        while (n.size() > 3 && n.back() == '/') n.pop_back();
        if (n.back() != '/') n.push_back('/');
        if (p.size() > n.size() && p.compare(0, n.size(), n) == 0) return true;
    }
    return false;
}

// macOS (and any POSIX test): the resolved library file must belong to root and must not be writable
// by group or others, otherwise another user of the machine could replace the code we load.
inline bool macLibraryOwnerOk(uint32_t uid, uint32_t mode, std::string *why) {
    if (uid != 0) {
        if (why) *why = "not owned by root";
        return false;
    }
    if (mode & 0022) {
        if (why) *why = "writable by group or others";
        return false;
    }
    return true;
}

// The resolved (realpath) file must be inside the framework directory.
inline bool macLibraryPathAllowed(const std::string &resolved) {
    const std::string dir = std::string(kMacClientFrameworkDir) + "/";
    if (resolved.size() <= dir.size() || resolved.compare(0, dir.size(), dir) != 0) return false;
    size_t i = dir.size();
    while (i <= resolved.size()) {
        size_t j = resolved.find('/', i);
        if (j == std::string::npos) j = resolved.size();
        const std::string comp = resolved.substr(i, j - i);
        if (comp == "." || comp == "..") return false;
        i = j + 1;
    }
    return true;
}

}  // namespace spn
