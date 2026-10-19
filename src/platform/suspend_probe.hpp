// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Portable daemon side of the suspend heartbeat (see src/common.hpp): while a UI measures the
// mouse itself it touches "<runtimeDir>/dofscroll-suspend" about every 200 ms; the daemon then
// sends no output. Used by the Windows and macOS daemons (Linux keeps its stricter probe in
// src/platform/linux/runtime_files.cpp). The file must be a regular file owned by the current user
// and not a link; a file older than SUSPEND_MAX_AGE_MS (UI crashed) is ignored.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>

#include "../common.hpp"
#include "fs.hpp"

namespace spn {

// Pure decision (tests): fresh means younger than SUSPEND_MAX_AGE_MS; a time stamp in the future is
// tolerated by the same amount (clock jumps), anything beyond that is not a heartbeat.
inline bool suspendHeartbeatFresh(int64_t mtimeNs, int64_t nowNs) {
    const int64_t age = nowNs - mtimeNs;
    const int64_t max = static_cast<int64_t>(SUSPEND_MAX_AGE_MS) * 1000000;
    return age >= -max && age <= max;
}

class FileSuspendProbe {
public:
    explicit FileSuspendProbe(const std::string &runtimeDir) : path_(suspendFilePathIn(runtimeDir)) {}
    bool operator()() const {
        if (path_.empty()) return false;
        fs::FileInfo li;
        if (!fs::lstatPath(path_, &li) || !li.isFile || li.isLink) return false;
        std::string ignored;  // owner check through the handle: no stat/open race
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        opt.requireOwner = true;
        if (!fs::readFileBounded(path_, 16, opt, &ignored).ok()) return false;
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        return suspendHeartbeatFresh(li.mtimeNs, now);
    }

private:
    std::string path_;
};

}  // namespace spn
