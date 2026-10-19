// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// SuspendHeartbeat: pauses the daemon's output while the UI evaluates the mouse itself (the axis
// assistant). The mechanism is the heartbeat file "<runtimeDir>/dofscroll-suspend" (src/common.hpp):
// the UI touches it at least every kIntervalMs, the daemon treats a file younger than
// SUSPEND_MAX_AGE_MS as "paused" and ignores an older one (UI crashed).
//
//   SuspendHeartbeat hb(runtimeDir);
//   hb.update(wantPause, nowNs);   // call from any periodic tick (e.g. the page's 30 Hz timer)
//   hb.stop();                     // page hidden, window closed, quit: removes the file
// The destructor stops too. The file is created user-only and never through a link (fs::
// touchFilePrivate); only a file this object touched is ever removed, and removal never follows a
// link. Toolkit-free: the caller owns the clock and the timer.
#pragma once
#include <cstdint>
#include <string>

#include "common.hpp"
#include "platform/fs.hpp"

namespace spn::ui {

class SuspendHeartbeat {
public:
    static constexpr int64_t kIntervalNs = 200LL * 1000000;  // well inside SUSPEND_MAX_AGE_MS (600 ms)

    explicit SuspendHeartbeat(std::string runtimeDir) : path_(suspendFilePathIn(runtimeDir)) {}
    SuspendHeartbeat(const SuspendHeartbeat &) = delete;
    SuspendHeartbeat &operator=(const SuspendHeartbeat &) = delete;
    ~SuspendHeartbeat() { stop(); }

    bool available() const { return !path_.empty(); }  // false without a runtime directory
    bool active() const { return active_; }
    bool fileMade() const { return made_; }
    const std::string &path() const { return path_; }

    // want = pause now. Refreshes the file when it is due, removes it when `want` ended.
    void update(bool want, int64_t nowNs) {
        if (!want) {
            if (active_ || made_) stop();
            return;
        }
        if (path_.empty()) return;
        if (!active_ || nowNs - lastNs_ >= kIntervalNs || nowNs < lastNs_) {
            active_ = true;
            lastNs_ = nowNs;
            made_ = fs::touchFilePrivate(path_);  // a refused (foreign/linked) file is never removed by us
        }
    }

    // Output may resume: removes the file (only if this object made it).
    void stop() {
        active_ = false;
        if (made_) fs::removeFile(path_);
        made_ = false;
    }

private:
    std::string path_;
    bool active_ = false, made_ = false;
    int64_t lastNs_ = 0;
};

}  // namespace spn::ui
