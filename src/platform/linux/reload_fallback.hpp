// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Reload request for a daemon that does not run under dofscroll.service (started by the interface when the unit could
// not be started, by hand, or on a system without a systemd user session): `systemctl --user kill` finds no unit
// then. The daemon writes its process id as "pid=<n>" into the status file in the private runtime directory; the
// client sends SIGHUP to that process, but only when
//   - the status file is a regular file of the current user that nobody else can write (no link is followed),
//   - /proc/<pid>/comm says "dofscroll" and /proc/<pid> belongs to the current user (a recycled pid of another program
//     is never signalled).
#pragma once
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../fs.hpp"
#include "../runtime_path.hpp"

namespace spn {

// The value of the line "pid=<n>" of the status file text; 0 when there is none or it is not a positive number.
inline long statusFilePid(const std::string &text) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.compare(0, 4, "pid=") != 0 || line.size() == 4 || line.size() > 4 + 10) continue;
        long n = 0;
        for (size_t i = 4; i < line.size(); ++i) {
            if (line[i] < '0' || line[i] > '9') return 0;
            n = n * 10 + (line[i] - '0');
        }
        return n;
    }
    return 0;
}

// Sends SIGHUP (through `hup`, which returns 0 on success) to the daemon named by the status file in runtimeDir.
// procRoot is "/proc" (tests pass a directory). false + *err when no daemon of this user can be found or signalled.
inline bool reloadByStatusPid(const std::string &runtimeDir, const std::string &procRoot, const std::function<int(long)> &hup,
                              std::string *err) {
    auto fail = [&](const char *m) {
        if (err) *err = m;
        return false;
    };
    const std::string path = checkedRuntimeFile(runtimeDir, RuntimeFile::StatusFile);
    if (path.empty()) return fail("no runtime directory");
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    opt.requirePrivate = true;
    std::string text;
    if (!fs::readFileBounded(path, 4096, opt, &text).ok()) return fail("no readable status file of a running daemon");
    const long pid = statusFilePid(text);
    if (pid <= 0) return fail("the status file names no process");
    const std::string dir = procRoot + "/" + std::to_string(pid);
    struct stat st;
    if (::stat(dir.c_str(), &st) != 0 || st.st_uid != ::geteuid()) return fail("the process of the status file is not a process of this user");
    std::string comm;
    if (!fs::readFileBounded(dir + "/comm", 64, fs::ReadOptions(), &comm).ok()) return fail("the process of the status file cannot be checked");
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == ' ')) comm.pop_back();
    if (comm != "dofscroll") return fail("the process of the status file is not dofscroll");
    if (hup(pid) != 0) return fail("cannot send SIGHUP to the daemon");
    return true;
}

}  // namespace spn
