// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Qt side of the autosave rule (src/ui/model/autosave.hpp): a timer that carries out the pending
// save when it is due, a synchronous flush for the moments when edits must not be lost (page
// switch, profile switch, window close, quit), and the status text for the footer. One Autosaver
// writes the configuration (owned by the MainWindow, reached through UiContext::autosave); the
// joystick mode editor and the button mode page have one each for their files.
//
//   autosave->requestImmediate();   // switch, drop-down, spin box, slider released, button row
//   autosave->requestDebounced();   // text field: written when typing paused (600 ms)
//   Autosaver::flushAll();          // synchronous, every Autosaver
//
// Every Autosaver reports through statusChanged(); the main window shows the text of the one it
// owns and the text that the others pass to report() on it ("Saved" for a few seconds,
// "Could not save: ..." until the next success).
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include "ui/model/autosave.hpp"
#include "ui/model/strings.hpp"

class Autosaver : public QObject {
    Q_OBJECT
public:
    Autosaver(spn::ui::Autosave::DirtyFn dirty, spn::ui::Autosave::SaveFn save, const spn::ui::StringProvider &strings,
              QObject *parent = nullptr);
    ~Autosaver() override;

    void requestImmediate();
    void requestDebounced();
    // Writes now if anything is pending or still unwritten. false if the write failed.
    bool flush();
    bool pending() const { return rule_.pending(); }
    const spn::ui::Autosave &rule() const { return rule_; }
    QString statusText() const { return status_; }
    bool failed() const { return rule_.state() == spn::ui::Autosave::State::Failed; }
    // Shows a status line of another writer (a page that saves its own file) in the same place.
    void report(const QString &text, bool error);
    // Flushes every Autosaver of the process (before the window closes or the application quits).
    // false if any of them failed.
    static bool flushAll();
    // Number of writes carried out so far (self-test).
    int writes() const { return rule_.attempts(); }

signals:
    void statusChanged(const QString &text, bool error);
    void written();  // after every write attempt, saved or not

private:
    qint64 now() const { return clock_.elapsed(); }
    void schedule();
    void onTimer();
    void afterWrite();

    spn::ui::Autosave rule_;
    const spn::ui::StringProvider &strings_;
    QElapsedTimer clock_;
    QTimer timer_, statusTimer_;
    QString status_;
};
