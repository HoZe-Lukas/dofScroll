// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// When the interface starts the daemon by itself, without any toolkit. After installing, the first
// start is "open dofScroll from the menu": nobody runs a command to start the daemon. The interface
// therefore starts it when none runs, judged from the status heartbeat (StatusModel::daemonRunning).
//
// A login autostart starts the daemon and the interface at the same moment, and the daemon needs a
// moment before its first heartbeat: the decision waits a grace period from the start of the
// interface and looks again. If the daemon is seen at any time before, it is never started (and not
// again when it stops later: a daemon that was stopped on purpose stays stopped). One attempt per run
// of the interface; if it fails, the setup guide and the Overview say that the daemon does not run.
// The daemon also guards itself against a second instance (platform/instance_lock.hpp), so a
// start that races with another one does no harm.
#pragma once
#include <cstdint>

namespace spn::ui {

class DaemonAutoStart {
public:
    enum class Step { Wait, Start, Done };
    static constexpr int64_t kGraceMs = 6000;

    explicit DaemonAutoStart(int64_t graceMs = kGraceMs) : graceMs_(graceMs) {}

    // Called about once per second. elapsedMs: time since the interface started; daemonRunning: a fresh
    // heartbeat is there right now.
    Step tick(int64_t elapsedMs, bool daemonRunning) {
        if (done_) return Step::Done;
        if (daemonRunning) {
            done_ = true;
            return Step::Done;
        }
        if (elapsedMs < graceMs_) return Step::Wait;
        done_ = true;
        attempted_ = true;
        return Step::Start;
    }
    bool done() const { return done_; }
    bool attempted() const { return attempted_; }  // the interface started the daemon itself
    // Still inside the grace period without a heartbeat: "not running yet" is expected.
    bool waiting(int64_t elapsedMs) const { return !done_ && elapsedMs < graceMs_; }

private:
    int64_t graceMs_;
    bool done_ = false, attempted_ = false;
};

}  // namespace spn::ui
