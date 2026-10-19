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

// The directories searched for helper programs, in order.
inline const std::vector<std::string> &trustedBinDirs() {
    static const std::vector<std::string> d = {"/usr/bin", "/usr/local/bin", "/bin"};
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

}  // namespace spn
