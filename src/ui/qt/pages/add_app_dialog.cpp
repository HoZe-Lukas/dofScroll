// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "add_app_dialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui_i18n.hpp"

using namespace spn;

namespace {
enum { kRoleMatch = Qt::UserRole, kRoleHasProfile };
}  // namespace

AddAppDialog::AddAppDialog(const ProfilesEnv &ctx, const std::vector<std::string> &configured, QWidget *parent)
    : QDialog(parent), ctx_(ctx) {
    setWindowTitle(T("Add application"));
    resize(720, 560);
    model_.setConfigured(configured);

    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(T("Pick a running program, capture the next one you focus, or type its match "
                              "string. The profile applies while that program has the focus."));
    intro->setWordWrap(true);
    l->addWidget(intro);

    auto *top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(T("Search by name, match string or window title"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(T("Search"));
    refresh_ = new QPushButton(T("Refresh"), this);
    refresh_->setToolTip(T("List the running programs again"));
    top->addWidget(search_, 1);
    top->addWidget(refresh_);
    l->addLayout(top);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({T("Name"), T("Match string"), T("Status")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    l->addWidget(list_, 1);

    busy_ = new QProgressBar(this);
    busy_->setRange(0, 0);  // busy indicator
    busy_->setTextVisible(false);
    busy_->setMaximumHeight(6);
    busy_->hide();
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    l->addWidget(busy_);
    l->addWidget(note_);

    auto *cap = new QHBoxLayout;
    capture_btn_ = new QPushButton(T("Capture next focused program"), this);
    capture_btn_->setToolTip(T("Click, then switch to the program within a few seconds"));
    countdown_ = new QLabel(this);
    cap->addWidget(capture_btn_);
    cap->addWidget(countdown_, 1);
    l->addLayout(cap);
    if (!ctx_.activeWindow) {
        capture_btn_->setEnabled(false);
        capture_btn_->setToolTip(T("Not available here: the focused program cannot be detected"));
    }

    l->addWidget(new QLabel(T("Match string:")));
    manual_ = new QLineEdit(this);
    manual_->setPlaceholderText(T("e.g. firefox"));
    manual_->setAccessibleName(T("Match string"));
    validation_ = new QLabel(this);
    validation_->setWordWrap(true);
    l->addWidget(manual_);
    l->addWidget(validation_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(T("Add"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    l->addWidget(buttons_);

    captureTimer_ = new QTimer(this);
    captureTimer_->setInterval(100);
    clock_.start();

    connect(search_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] {
        const QTreeWidgetItem *it = list_->currentItem();
        if (it) manual_->setText(it->data(0, kRoleMatch).toString());
    });
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (buttons_->button(QDialogButtonBox::Ok)->isEnabled()) accept();
    });
    connect(manual_, &QLineEdit::textChanged, this, [this] { validate(); });
    connect(capture_btn_, &QPushButton::clicked, this, [this] { startCapture(); });
    connect(captureTimer_, &QTimer::timeout, this, [this] { tickCapture(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setTabOrder(search_, refresh_);
    setTabOrder(refresh_, list_);
    setTabOrder(list_, capture_btn_);
    setTabOrder(capture_btn_, manual_);

    validate();
    refresh();
}

AddAppDialog::~AddAppDialog() { ++generation_; }

QString AddAppDialog::matchString() const {
    std::string norm;
    if (validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, nullptr)) return qs8(norm);
    return QString();
}

// The listing runs in a worker thread (X11 round trips, process queries) so the dialog stays
// responsive; the result comes back through a queued call and is dropped if the dialog is gone
// or a newer listing was started.
void AddAppDialog::refresh() {
    if (listing_) return;
    listing_ = true;
    busy_->show();
    refresh_->setEnabled(false);
    note_->setText(T("Listing running programs..."));
    const int gen = ++generation_;
    auto factory = ctx_.runningApps;
    QPointer<AddAppDialog> self(this);
    QThread *th = QThread::create([factory, self, gen] {
        std::vector<RunningApp> apps;
        std::string err;
        bool ok = false;
        if (factory) {
            std::unique_ptr<RunningAppsProvider> p = factory();
            if (p) ok = p->list(&apps, &err);
        } else {
            err = "not available";
        }
        const QString qerr = QString::fromUtf8(err.data(), qsizetype(err.size()));
        if (!self) return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, gen, ok, qerr, apps = std::move(apps)]() mutable {
                if (self && self->generation_ == gen) self->applyResult(ok, qerr, std::move(apps));
            },
            Qt::QueuedConnection);
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void AddAppDialog::applyResult(bool ok, const QString &err, std::vector<RunningApp> apps) {
    listing_ = false;
    busy_->hide();
    refresh_->setEnabled(true);
    model_.setApps(std::move(apps));
    refill();
    if (!ok)
        note_->setText(T("The list of running programs is not available:") + QLatin1Char(' ') + err + QLatin1Char(' ') +
                       T("You can still capture a program or type its match string."));
    else
        note_->setText(T("Programs marked \"Already has a profile\" can be selected again; the existing "
                         "profile is then opened."));
}

void AddAppDialog::refill() {
    list_->clear();
    for (const AppRow &r : model_.rows(search_->text().toUtf8().toStdString())) {
        auto *it = new QTreeWidgetItem(list_);
        it->setText(0, qs8(r.app.displayName.empty() ? r.app.matchString : r.app.displayName));
        it->setText(1, qs8(r.app.matchString));
        if (r.hasProfile) it->setText(2, T("Already has a profile"));
        if (!r.app.windowTitle.empty()) it->setToolTip(0, qs8(r.app.windowTitle));
        it->setData(0, kRoleMatch, qs8(r.app.matchString));
        it->setData(0, kRoleHasProfile, r.hasProfile);
    }
}

void AddAppDialog::startCapture() {
    if (!ctx_.activeWindow) return;
    capture_.start(clock_.elapsed(), ctx_.activeWindow());
    capture_btn_->setEnabled(false);
    captureTimer_->start();
    tickCapture();
}

void AddAppDialog::stopCapture() {
    captureTimer_->stop();
    capture_btn_->setEnabled(bool(ctx_.activeWindow));
}

void AddAppDialog::tickCapture() {
    const int64_t now = clock_.elapsed();
    if (capture_.tick(ctx_.activeWindow(), now)) {
        stopCapture();
        manual_->setText(qs8(capture_.result()));
        countdown_->setText(T("Captured:") + QLatin1Char(' ') + qs8(capture_.result()));
        return;
    }
    if (capture_.state() == CaptureNextFocus::State::TimedOut) {
        stopCapture();
        countdown_->setText(T("No other program was focused in time."));
        return;
    }
    const int secs = int((capture_.remainingMs(now) + 999) / 1000);
    countdown_->setText(T("Switch to the program now... %1 s").arg(secs));
}

void AddAppDialog::validate() {
    std::string norm, err;
    const bool ok = validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, &err);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
    if (ok) {
        validation_->setText(model_.alreadyConfigured(norm) ? T("This program already has a profile.") : QString());
    } else {
        validation_->setText(manual_->text().isEmpty() ? QString() : T(err.c_str()));
    }
}
