// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Termination signals of the interface (Linux and macOS): `systemctl --user stop|restart dofscroll-ui`, a logout and
// Ctrl-C end the process with SIGTERM, SIGINT or SIGHUP, whose default action kills it at once, so an edit that the
// autosave still holds (600 ms to 3 s) would be lost. SignalFlush turns these signals into a call on the Qt event
// loop (self-pipe and QSocketNotifier: the handler itself only writes one byte, which is async-signal-safe), where
// the callback flushes the pending edits, saves the window state and quits. On Windows it does nothing.
#pragma once
#include <QObject>

#include <functional>

class QSocketNotifier;

class SignalFlush : public QObject {
    Q_OBJECT
public:
    // `onSignal(signo)` runs on the event loop of the thread that created this object. Only one object may exist.
    explicit SignalFlush(std::function<void(int)> onSignal, QObject *parent = nullptr);
    ~SignalFlush() override;  // the previous handlers are restored
    bool active() const { return notifier_ != nullptr; }

private:
    void drain();

    std::function<void(int)> onSignal_;
    QSocketNotifier *notifier_ = nullptr;
};
