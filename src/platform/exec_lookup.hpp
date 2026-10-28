// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Looks up a helper program (such as kdotool) in a fixed list of system directories. The daemon
// never searches PATH for a program it runs: the environment cannot name what gets executed.
// POSIX only (access, stat).
#pragma once
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace spn {

// Directories of the operating system's own tools (systemctl, launchctl, ...), in order. Nothing
// an administrator or a package installs on its own (/usr/local) is in this list.
inline const std::vector<std::string> &systemBinDirs() {
    static const std::vector<std::string> d = {"/usr/bin", "/bin", "/usr/sbin", "/sbin"};
    return d;
}

// The directories searched for optional helper programs (kdotool): the system directories first,
// /usr/local/bin last.
inline const std::vector<std::string> &trustedBinDirs() {
    static const std::vector<std::string> d = {"/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin"};
    return d;
}

// Absolute path of the executable regular file `prog` in the first of `dirs` that has one; "" if
// there is none. `prog` must be a plain file name (no slash, not empty, not "." or ".."); relative
// or empty directory entries are skipped, so nothing relative to the working directory is run.
inline std::string findExecutableIn(const std::string &prog, const std::vector<std::string> &dirs) {
    if (prog.empty() || prog == "." || prog == ".." || prog.find('/') != std::string::npos) return std::string();
    for (const std::string &dir : dirs) {
        if (dir.empty() || dir[0] != '/') continue;
        const std::string full = (dir.back() == '/' ? dir : dir + "/") + prog;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(full.c_str(), X_OK) == 0) return full;
    }
    return std::string();
}

// Like findExecutableIn, but a candidate only counts when it is owned by `owner` and neither group-
// nor other-writable, so a planted or tampered file is skipped and the search goes on.
inline std::string findOwnedExecutableIn(const std::string &prog, const std::vector<std::string> &dirs,
                                         uid_t owner) {
    if (prog.empty() || prog == "." || prog == ".." || prog.find('/') != std::string::npos) return std::string();
    for (const std::string &dir : dirs) {
        if (dir.empty() || dir[0] != '/') continue;
        const std::string full = (dir.back() == '/' ? dir : dir + "/") + prog;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || access(full.c_str(), X_OK) != 0) continue;
        if (st.st_uid != owner || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) continue;
        return full;
    }
    return std::string();
}

// Absolute path of the helper program `prog` (systemctl, launchctl, ...): a root-owned, not
// group/other-writable file in the system directories only; "" if there is none. The only way the
// daemon and the interface find a system tool.
inline std::string findHelper(const std::string &prog) { return findOwnedExecutableIn(prog, systemBinDirs(), 0); }

}  // namespace spn
