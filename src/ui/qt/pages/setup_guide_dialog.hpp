// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first-run setup guide (src/ui/model/setup_guide_model.hpp): a dialog with a few steps and live
// checks (daemon running, SpaceMouse found, Start at login, on macOS the two permissions, Try it).
// Shown with the settings window at the first start, and again from the Overview ("Show setup guide").
// "Skip" and "Finish" close it (the model remembers that it was seen). Nothing in the system is changed while the
// dialog is open or when it is skipped: the check box "Start at login" only records a wish (AutostartConsent,
// src/ui/model/setup_guide_model.hpp), and "Finish" applies it (issue #276).
#pragma once
#include <QDialog>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "platform/autostart_platform.hpp"
#include "ui/model/setup_guide_model.hpp"
#include "ui_context.hpp"

class DaemonStarter;
class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QThread;
class QTimer;

class SetupGuideDialog : public QDialog {
    Q_OBJECT
public:
    using AutostartReader = std::function<spn::autostart::AutostartState(std::string *detail)>;

    // firstRun: the check box "Start at login" starts ticked (the default of the guide); otherwise it shows the state
    // of the system. Either way nothing is changed before "Finish" is pressed.
    // `starter` (may be null) tells whether the daemon is still being started. ctx and starter must outlive the dialog.
    // `reader`: where the state of "Start at login" is read from (default: the platform's queryAutostart; tests).
    SetupGuideDialog(const UiContext &ctx, const DaemonStarter *starter, bool firstRun, QWidget *parent = nullptr,
                     AutostartReader reader = AutostartReader());
    ~SetupGuideDialog() override;

    void refresh();  // read the live state again (once per second while shown)
    const std::vector<spn::ui::SetupStep> &steps() const { return steps_; }
    const spn::ui::SetupStep *step(const QString &id) const;
    QCheckBox *autostartBox() const { return autostart_; }
    QPushButton *skipButton() const { return skip_; }
    QPushButton *finishButton() const { return finish_; }
    QString commandOf(const QString &id) const;       // the copy-able command shown in a step ("" = none)
    QPushButton *actionButtonOf(const QString &id) const;  // the button of a step (null = none)
    bool autostartBusy() const { return busy_; }
    void waitIdle();  // lets the worker of "Start at login" finish (tests)
    // The width the rows need without a horizontal scroll bar (tests).
    bool hasHorizontalScrollBar() const;

signals:
    void skipRequested();    // "Skip the guide" or the window's close button
    void finishRequested();  // "Finish"

protected:
    void reject() override;  // Escape, the close button: counts as "Skip"
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Row {
        QFrame *frame = nullptr;
        QLabel *title = nullptr, *state = nullptr, *text = nullptr;
        QLineEdit *command = nullptr;
        QPushButton *copy = nullptr, *action = nullptr;
        QWidget *commandRow = nullptr;
    };
    struct AutoResult {
        spn::autostart::AutostartState state = spn::autostart::AutostartState::Error;
        std::string detail;
        bool setOk = true;
        std::string setErr;
    };
    void build(const std::vector<spn::ui::SetupStep> &first);
    void runAction(spn::ui::StepAction a);
    void runAutostart(bool change, bool enable);  // reads the state; with `change` first applies it (only from Finish)
    void finishNow();
    void onAutostartDone(const AutoResult &r);
    void onSpacenavdProbed(spn::ui::ServiceState state);
    void update(const std::vector<spn::ui::SetupStep> &steps);

    UiContext ctx_;
    const DaemonStarter *starter_;
    std::vector<spn::ui::SetupStep> steps_;
    std::map<std::string, Row> rows_;
    QLabel *summary_ = nullptr, *message_ = nullptr;
    QCheckBox *autostart_ = nullptr;
    QPushButton *skip_ = nullptr, *finish_ = nullptr;
    class QScrollArea *scroll_ = nullptr;
    QTimer *timer_ = nullptr;
    QPointer<QThread> thread_;
    QPointer<QThread> probeThread_;  // asks the runner whether spacenavd runs (a process: not on the GUI thread)
    bool probeBusy_ = false;
    AutostartReader reader_;
    bool busy_ = false, closing_ = false, finishing_ = false;
    spn::ui::AutostartConsent consent_;
    // the last state read for "Start at login"
    bool autostartKnown_ = false, autostartOn_ = false;
    std::string autostartDetail_;
    std::string autostartSetError_;
    qint64 spacenavdAskedAt_ = -1;  // ms since the epoch
    spn::ui::ServiceState spacenavd_ = spn::ui::ServiceState::Unknown;
};
