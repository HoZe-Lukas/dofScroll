// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Starts the daemon when the interface finds none running (after installing, the first start is
// "open dofscroll from the menu"). The decision is src/ui/model/daemon_autostart.hpp: the status
// heartbeat is read once per second; after the grace period without a heartbeat the daemon is started
// through ActionRunner::startDaemon, once per run of the interface.
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <string>

#include "ui/model/action_runner.hpp"
#include "ui/model/daemon_autostart.hpp"

class DaemonStarter : public QObject {
    Q_OBJECT
public:
    // `runner` must outlive this object. runtimeDir: where the status file is (may be empty: no heartbeat).
    DaemonStarter(spn::ui::ActionRunner &runner, std::string runtimeDir, QObject *parent = nullptr,
                  int64_t graceMs = spn::ui::DaemonAutoStart::kGraceMs);
    void start();  // begin watching (once per second)
    // One step of the decision at `elapsedMs` since the start; the timer calls it, tests call it with a made-up clock.
    // `heartbeat` is read from the status file when not given (-1).
    spn::ui::DaemonAutoStart::Step tick(int64_t elapsedMs, int heartbeat = -1);
    bool daemonSeen() const { return running_; }      // a fresh heartbeat was there at the last look
    bool attempted() const { return auto_.attempted(); }  // this interface started the daemon
    bool startFailed() const { return startFailed_; }     // the start command itself failed
    // "Not running yet" is expected: inside the grace period, or right after the start.
    bool waiting() const;
    // The daemon was started by this interface and there is still no heartbeat after a while.
    bool failed() const;
    QString lastError() const { return error_; }

signals:
    void daemonStarted(bool ok);

private:
    spn::ui::ActionRunner &runner_;
    std::string runtimeDir_;
    spn::ui::DaemonAutoStart auto_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool running_ = false, startFailed_ = false;
    int64_t attemptAt_ = 0, lastElapsed_ = 0;
    QString error_;
};
