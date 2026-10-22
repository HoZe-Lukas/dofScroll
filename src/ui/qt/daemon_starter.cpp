// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "daemon_starter.hpp"

#include <cstdio>
#include <ctime>

#include "ui/model/runtime_files.hpp"
#include "ui/model/status_model.hpp"

namespace {
constexpr int64_t kStartedGraceMs = 10000;  // after the start the heartbeat may take this long
}

DaemonStarter::DaemonStarter(spn::ui::ActionRunner &runner, std::string runtimeDir, QObject *parent, int64_t graceMs)
    : QObject(parent), runner_(runner), runtimeDir_(std::move(runtimeDir)), auto_(graceMs) {
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(clock_.elapsed()); });
}

void DaemonStarter::start() {
    clock_.start();
    tick(0);
    if (!(auto_.done() && running_)) timer_.start();
}

spn::ui::DaemonAutoStart::Step DaemonStarter::tick(int64_t elapsedMs, int heartbeat) {
    lastElapsed_ = elapsedMs;
    if (heartbeat < 0) {
        std::string text;
        spn::ui::StatusModel m;
        const bool readable = spn::ui::readStatusFile(runtimeDir_, &text);
        m.update(text, readable, static_cast<int64_t>(std::time(nullptr)));
        heartbeat = m.daemonRunning() ? 1 : 0;
    }
    running_ = heartbeat > 0;
    const spn::ui::DaemonAutoStart::Step step = auto_.tick(elapsedMs, running_);
    if (step == spn::ui::DaemonAutoStart::Step::Start) {
        attemptAt_ = elapsedMs;
        std::string err;
        const bool ok = runner_.startDaemon(&err);
        startFailed_ = !ok;
        error_ = QString::fromStdString(err);
        std::fprintf(stderr, "dofscroll-ui: no daemon running: started it: %s%s%s\n", ok ? "ok" : "failed",
                     ok ? "" : " - ", ok ? "" : err.c_str());
        emit daemonStarted(ok);
    }
    if (running_ && auto_.done()) timer_.stop();  // nothing more to watch
    return step;
}

bool DaemonStarter::waiting() const {
    if (running_) return false;
    if (auto_.waiting(lastElapsed_)) return true;
    return auto_.attempted() && !startFailed_ && lastElapsed_ - attemptAt_ < kStartedGraceMs;
}

bool DaemonStarter::failed() const {
    if (running_ || !auto_.attempted()) return false;
    return startFailed_ || lastElapsed_ - attemptAt_ >= kStartedGraceMs;
}
