// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The single entry point for runtime file paths of the daemon: the directory comes from the
// environment (XDG_RUNTIME_DIR) or the OS rules, the file name only from this allow-list. Nothing
// else reaches open()/unlink()/rename(): the directory must be absolute and free of ".." parts, "."
// parts, empty parts and control characters, and the file component is one of four fixed names
// (no separator can occur in it). Ownership and permissions of the directory are checked by the
// caller (runtimeDirTrusted); files are opened with O_NOFOLLOW.
#pragma once
#include <string>

namespace spn {

enum class RuntimeFile { LogFile, StatusFile, SuspendFile, LockFile };

inline const char *runtimeFileName(RuntimeFile f) {
    switch (f) {
        case RuntimeFile::LogFile: return "dofscroll.log";
        case RuntimeFile::StatusFile: return "dofscroll-status";
        case RuntimeFile::SuspendFile: return "dofscroll-suspend";
        case RuntimeFile::LockFile: return "dofscroll.lock";
    }
    return "";
}

// Lexical check of the runtime directory (POSIX absolute path).
inline bool runtimeDirLexicallyValid(const std::string &dir) {
    if (dir.size() < 2 || dir.size() > 1024 || dir[0] != '/' || dir.back() == '/') return false;
    size_t start = 1;
    while (start <= dir.size()) {
        size_t end = dir.find('/', start);
        if (end == std::string::npos) end = dir.size();
        const std::string part = dir.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    for (char c : dir)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

// "<dir>/<allow-listed name>" or "" when the directory is not acceptable.
inline std::string checkedRuntimeFile(const std::string &dir, RuntimeFile f) {
    if (!runtimeDirLexicallyValid(dir)) return std::string();
    return dir + "/" + runtimeFileName(f);
}

}  // namespace spn
