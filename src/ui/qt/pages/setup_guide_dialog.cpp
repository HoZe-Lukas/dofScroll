// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "setup_guide_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "daemon_starter.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/general_page_model.hpp"
#include "ui_i18n.hpp"

namespace {

QLabel *wrapLabel(const QString &text = QString()) {
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

// "Done" with a check mark, "Needs attention" with a warning sign, "To do" plain: always in words too.
QString stateText(const spn::ui::SetupStep &s) {
    if (s.status.empty()) return QString();
    const QString words = qs8(s.status);
    if (s.state == spn::ui::StepState::Done) return mark(spn::ui::StatusKind::Success, words);
    if (s.state == spn::ui::StepState::Problem) return mark(spn::ui::StatusKind::Warning, words);
    return words;
}

}  // namespace

SetupGuideDialog::SetupGuideDialog(const UiContext &ctx, const DaemonStarter *starter, bool firstRun, QWidget *parent,
                                   AutostartReader reader)
    : QDialog(parent), ctx_(ctx), starter_(starter), reader_(std::move(reader)), consent_(firstRun) {
    using namespace spn::ui::tokens;
    if (!reader_) reader_ = [](std::string *detail) { return spn::autostart::queryAutostart(detail); };
    setWindowTitle(T("Set up dofscroll"));
    setObjectName(QStringLiteral("setup-guide"));
    QSize size(660, 700);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        size = size.boundedTo(QSize(avail.width() * 9 / 10, avail.height() * 9 / 10));
    }
    resize(size);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPageMargin, kSpaceLg, kPageMargin, kSpaceLg);
    outer->setSpacing(kSpaceMd);
    auto *heading = new QLabel(T("Welcome to dofscroll"));
    heading->setProperty("heading", QStringLiteral("page"));
    heading->setAccessibleName(T("Setup guide title"));
    outer->addWidget(heading);
    summary_ = wrapLabel();
    summary_->setForegroundRole(QPalette::PlaceholderText);
    summary_->setAccessibleName(T("Setup progress"));
    outer->addWidget(summary_);

    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll_, 1);

    message_ = wrapLabel();
    message_->setVisible(false);
    outer->addWidget(message_);

    auto *buttons = new QHBoxLayout;
    skip_ = new QPushButton(T("Skip the guide"));
    skip_->setToolTip(T("Close the guide without finishing it. \"Show setup guide\" on the Overview opens it again."));
    finish_ = new QPushButton(T("Finish"));
    finish_->setDefault(true);
    finish_->setToolTip(T("Close the guide. \"Show setup guide\" on the Overview opens it again."));
    buttons->addStretch(1);
    buttons->addWidget(skip_);
    buttons->addWidget(finish_);
    outer->addLayout(buttons);
    connect(skip_, &QPushButton::clicked, this, [this] {
        closing_ = true;
        emit skipRequested();
        hide();
    });
    connect(finish_, &QPushButton::clicked, this, [this] { finishNow(); });

    spn::ui::SetupInputs in;
    in.os = spn::ui::hostOs();
    build(spn::ui::buildSetupGuide(in, *ctx_.strings));

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
    runAutostart(false, false);  // only reads the state: nothing is changed before the user presses Finish
    refresh();
}

SetupGuideDialog::~SetupGuideDialog() {
    if (thread_) thread_->wait();  // the worker only reads and changes the autostart entries
    if (probeThread_) probeThread_->wait();
}

void SetupGuideDialog::onSpacenavdProbed(spn::ui::ServiceState state) {
    probeBusy_ = false;
    if (state == spacenavd_) return;
    spacenavd_ = state;
    refresh();  // the answer changes what the device step says
}

void SetupGuideDialog::build(const std::vector<spn::ui::SetupStep> &first) {
    using namespace spn::ui::tokens;
    auto *host = new QWidget;
    auto *col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, kSpaceSm, 0);
    col->setSpacing(kSpaceMd);
    int number = 0;
    for (const spn::ui::SetupStep &s : first) {
        Row r;
        r.frame = new QFrame;
        r.frame->setFrameShape(QFrame::StyledPanel);
        r.frame->setProperty("card", true);
        r.frame->setObjectName(qs8("guide-step-" + s.id));
        auto *cl = new QVBoxLayout(r.frame);
        cl->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
        cl->setSpacing(kSpaceSm);
        auto *head = new QHBoxLayout;
        r.title = wrapLabel(QString::number(++number) + QStringLiteral(". ") + qs8(s.title));
        QFont f = r.title->font();
        f.setBold(true);
        r.title->setFont(f);
        r.state = new QLabel;
        r.state->setObjectName(qs8("guide-status-" + s.id));
        r.state->setAccessibleName(qs8(s.title) + QStringLiteral(": ") + T("State"));
        head->addWidget(r.title, 1);
        head->addWidget(r.state, 0, Qt::AlignTop);
        cl->addLayout(head);
        r.text = wrapLabel();
        cl->addWidget(r.text);
        if (s.id == "autostart") {
            autostart_ = new QCheckBox(T("Start dofscroll at login"));
            autostart_->setAccessibleName(T("Start dofscroll at login"));
            autostart_->setAccessibleDescription(T("Nothing is changed until you press Finish."));
            autostart_->setToolTip(T("Nothing is changed until you press Finish."));
            autostart_->setChecked(consent_.wish());  // the visible default; the system is read in the background
            autostart_->setEnabled(false);
            // a click is only a choice: Finish applies it, Skip and Escape drop it
            connect(autostart_, &QCheckBox::clicked, this, [this](bool on) {
                consent_.setWish(on);
                refresh();
            });
            cl->addWidget(autostart_);
        }
        // the copy-able command (a fallback only)
        r.commandRow = new QWidget;
        auto *cr = new QHBoxLayout(r.commandRow);
        cr->setContentsMargins(0, 0, 0, 0);
        r.command = new QLineEdit;
        r.command->setReadOnly(true);
        r.command->setAccessibleName(T("Command to copy"));
        r.copy = new QPushButton(T("Copy"));
        r.copy->setAccessibleName(T("Copy the command"));
        QLineEdit *cmd = r.command;
        connect(r.copy, &QPushButton::clicked, this, [cmd] { QApplication::clipboard()->setText(cmd->text()); });
        cr->addWidget(r.command, 1);
        cr->addWidget(r.copy);
        r.commandRow->setVisible(false);
        cl->addWidget(r.commandRow);
        r.action = new QPushButton;
        r.action->setVisible(false);
        const spn::ui::StepAction act = s.action;
        connect(r.action, &QPushButton::clicked, this, [this, act] { runAction(act); });
        cl->addWidget(r.action, 0, Qt::AlignLeft);
        col->addWidget(r.frame);
        rows_[s.id] = r;
    }
    col->addStretch(1);
    scroll_->setWidget(host);
}

void SetupGuideDialog::update(const std::vector<spn::ui::SetupStep> &steps) {
    steps_ = steps;
    for (const spn::ui::SetupStep &s : steps) {
        auto it = rows_.find(s.id);
        if (it == rows_.end()) continue;
        Row &r = it->second;
        const QString st = stateText(s);
        if (r.state->text() != st) r.state->setText(st);
        const QString text = qs8(s.text);
        if (r.text->text() != text) r.text->setText(text);
        const bool hasCommand = !s.command.empty();
        r.commandRow->setVisible(hasCommand);
        if (hasCommand && r.command->text() != qs8(s.command)) r.command->setText(qs8(s.command));
        // The check box of "Start at login" is the control of its step; every other action is a button.
        const bool button = !s.actionLabel.empty() && s.state != spn::ui::StepState::Done;
        r.action->setVisible(button);
        if (button) {
            const QString label = qs8(s.actionLabel);
            if (r.action->text() != label) r.action->setText(label);
            r.action->setAccessibleName(label);
        }
    }
    size_t total = 0;
    const size_t done = spn::ui::setupStepsDone(steps, &total);
    summary_->setText(done == total
                          ? T("Everything is set up. You can close this guide; it can be opened again from the Overview.")
                          : T("%1 of %2 steps are done. The checks update by themselves.").arg(done).arg(total));
}

void SetupGuideDialog::refresh() {
    if (!ctx_.status || !ctx_.strings) return;
    spn::ui::SetupInputs in = spn::ui::setupInputsFrom(*ctx_.status, spn::ui::hostOs());
    in.waitingForDaemon = !in.daemonRunning && starter_ && starter_->waiting();
    in.daemonStartFailed = !in.daemonRunning && starter_ && starter_->failed();
    if (spn::ui::hostOs() == spn::ui::Os::Linux && in.daemonRunning && !in.deviceSeen && ctx_.runner) {
        // asked at most every few seconds, on a worker thread: systemctl is a process (up to 10 s) and the window
        // shows the last answer meanwhile
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!probeBusy_ && (spacenavdAskedAt_ < 0 || now - spacenavdAskedAt_ > 4000 || now < spacenavdAskedAt_)) {
            spacenavdAskedAt_ = now;
            probeBusy_ = true;
            spn::ui::ActionRunner *runner = ctx_.runner;
            QThread *t = QThread::create([this, runner] {
                const spn::ui::ServiceState st = runner->spacenavdState();
                QMetaObject::invokeMethod(this, [this, st] { onSpacenavdProbed(st); }, Qt::QueuedConnection);
            });
            probeThread_ = t;
            connect(t, &QThread::finished, t, &QObject::deleteLater);
            t->start();
        }
        in.spacenavd = spacenavd_;
    }
    in.autostartKnown = autostartKnown_;
    in.autostartOn = autostartOn_;
    in.autostartPending = consent_.pending();
    in.autostartDetail = autostartDetail_;
    if (spn::ui::hostOs() == spn::ui::Os::MacOS)
        in.daemonPath = spn::autostart::macBundleDaemonPath(QCoreApplication::applicationDirPath().toStdString());
    update(spn::ui::buildSetupGuide(in, *ctx_.strings));
}

const spn::ui::SetupStep *SetupGuideDialog::step(const QString &id) const {
    for (const spn::ui::SetupStep &s : steps_)
        if (qs8(s.id) == id) return &s;
    return nullptr;
}

QString SetupGuideDialog::commandOf(const QString &id) const {
    const spn::ui::SetupStep *s = step(id);
    return s ? qs8(s->command) : QString();
}

QPushButton *SetupGuideDialog::actionButtonOf(const QString &id) const {
    auto it = rows_.find(id.toStdString());
    return it != rows_.end() && it->second.action->isVisibleTo(const_cast<SetupGuideDialog *>(this)) ? it->second.action : nullptr;
}

void SetupGuideDialog::runAction(spn::ui::StepAction a) {
    if (!ctx_.runner) return;
    std::string err;
    bool ok = true;
    switch (a) {
        case spn::ui::StepAction::StartDaemon: ok = ctx_.runner->startDaemon(&err); break;
        case spn::ui::StepAction::OpenAccessibility:
            ok = ctx_.runner->openSystemSettings(spn::ui::permissionPaneUrl(spn::ui::PermissionPane::Accessibility), &err);
            break;
        case spn::ui::StepAction::OpenInputMonitoring:
            ok = ctx_.runner->openSystemSettings(spn::ui::permissionPaneUrl(spn::ui::PermissionPane::InputMonitoring), &err);
            break;
        case spn::ui::StepAction::Autostart:
        case spn::ui::StepAction::None: return;
    }
    message_->setText(ok ? QString() : mark(spn::ui::StatusKind::Error, qs8(err)));
    message_->setVisible(!ok);
    refresh();
}

// Reads (and first changes, if requested) the autostart state on a worker thread: the system calls can
// take a moment (systemctl, registry, launchctl) and must not freeze the window.
void SetupGuideDialog::runAutostart(bool change, bool enable) {
    if (busy_) return;
    busy_ = true;
    if (autostart_) autostart_->setEnabled(false);
    if (finish_) finish_->setEnabled(false);  // until the state is read (or the change is applied)
    spn::ui::ActionRunner *runner = ctx_.runner;
    const AutostartReader reader = reader_;
    QThread *t = QThread::create([this, runner, reader, change, enable] {
        AutoResult r;
        if (change && runner) r.setOk = runner->setAutostart(enable, &r.setErr);
        r.state = reader(&r.detail);
        QMetaObject::invokeMethod(this, [this, r] { onAutostartDone(r); }, Qt::QueuedConnection);
    });
    thread_ = t;
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void SetupGuideDialog::onAutostartDone(const AutoResult &r) {
    using spn::autostart::AutostartState;
    busy_ = false;
    if (finish_) finish_->setEnabled(true);
    autostartKnown_ = true;
    autostartOn_ = r.state == AutostartState::Enabled;
    autostartDetail_.clear();
    if (r.state == AutostartState::Unavailable || r.state == AutostartState::Error) {
        const spn::ui::AutostartView v = spn::ui::autostartView(r.state, r.detail, *ctx_.strings);
        autostartDetail_ = r.detail.empty() ? v.text : r.detail;
    }
    if (!r.setOk) {
        message_->setText(mark(spn::ui::StatusKind::Error, T("Could not change the setting: ") + qs8(r.setErr)));
        message_->setVisible(true);
    }
    const bool changeable = r.state != AutostartState::Unavailable && r.state != AutostartState::Error;
    consent_.systemState(true, autostartOn_, r.state == AutostartState::Partial, changeable);
    if (autostart_) {
        QSignalBlocker b(autostart_);
        // the box shows the wish (the default of a first run, a click), otherwise what the system says
        const bool shown = consent_.wish();
        autostart_->setCheckState(!consent_.active() && r.state == AutostartState::Partial ? Qt::PartiallyChecked
                                  : shown ? Qt::Checked : Qt::Unchecked);
        autostart_->setEnabled(changeable);
    }
    if (finishing_) {
        finishing_ = false;
        if (r.setOk) {  // applied: now the guide may close
            closing_ = true;
            emit finishRequested();
            hide();
        }  // else the message is shown and the guide stays open: try again, or skip
    }
    refresh();
}

// "Finish": the one place where the system is changed (issue #276), and only for a choice that differs from it.
void SetupGuideDialog::finishNow() {
    const spn::ui::AutostartConsent::Change c = consent_.confirm();
    if (!c.needed) {
        closing_ = true;
        emit finishRequested();
        hide();
        return;
    }
    finishing_ = true;
    message_->setVisible(false);
    runAutostart(true, c.enable);
    if (!busy_) finishing_ = false;  // a read was still running: the button is disabled for that time, so this is only a guard
}

void SetupGuideDialog::waitIdle() {
    QElapsedTimer t;
    t.start();
    while ((busy_ || probeBusy_) && t.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool SetupGuideDialog::hasHorizontalScrollBar() const {
    return scroll_->horizontalScrollBar()->isVisible() && scroll_->horizontalScrollBar()->maximum() > 0;
}

void SetupGuideDialog::reject() {
    // Escape and the window's own close button count as "Skip" (once)
    if (!closing_) emit skipRequested();
    closing_ = false;
    QDialog::reject();
}

void SetupGuideDialog::showEvent(QShowEvent *e) {
    closing_ = false;
    refresh();
    timer_->start();
    QDialog::showEvent(e);
}

void SetupGuideDialog::hideEvent(QHideEvent *e) {
    timer_->stop();
    QDialog::hideEvent(e);
}
