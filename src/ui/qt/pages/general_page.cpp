// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "general_page.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "platform/autostart_platform.hpp"
#include "ui/model/general_page_model.hpp"
#include "ui/model/ui_settings.hpp"

namespace {

using namespace spn::ui;

struct AutoResult {
    spn::autostart::AutostartState state = spn::autostart::AutostartState::Error;
    std::string detail;
    bool setOk = true;
    std::string setErr;
};

class GeneralPage : public QWidget {
public:
    explicit GeneralPage(PageContext &ctx) : QWidget(nullptr), ctx_(ctx) {
        auto *col = new QVBoxLayout(this);

        // ---- start at login ----
        auto *loginBox = new QGroupBox(T("Start at login"));
        auto *ll = new QVBoxLayout(loginBox);
        autostart_ = new QCheckBox(T("Start dofscroll at login"));
        autostart_->setEnabled(false);
        autostart_->setToolTip(T("Checking..."));
        ll->addWidget(autostart_);
        autostartMsg_ = makeNote();
        autostartMsg_->setText(T("Checking..."));
        ll->addWidget(autostartMsg_);
        auto *loginHint = makeNote();
        loginHint->setText(T("Affects the next login only; a running program is not started or stopped."));
        ll->addWidget(loginHint);
        QObject::connect(autostart_, &QCheckBox::clicked, this, [this](bool on) { runAutostart(true, on); });
        col->addWidget(loginBox);

        // ---- language ----
        auto *langBox = new QGroupBox(T("Language"));
        auto *lg = new QVBoxLayout(langBox);
        language_ = new QComboBox;
        language_->addItem(T("System default"), int(Language::System));
        language_->addItem(QStringLiteral("English"), int(Language::English));
        language_->addItem(QStringLiteral("Deutsch"), int(Language::German));
        language_->setAccessibleName(T("Language"));
        const Language cur = loadUiSettings(uiSettingsPath()).language;
        language_->setCurrentIndex(language_->findData(int(cur)));
        lg->addWidget(language_);
        languageMsg_ = makeNote();
        languageMsg_->setText(T("Takes effect the next time dofscroll-ui is started."));
        lg->addWidget(languageMsg_);
        QObject::connect(language_, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onLanguage(); });
        col->addWidget(langBox);

        // ---- status ----
        auto *statusBox = new QGroupBox(T("Status"));
        auto *sl = new QVBoxLayout(statusBox);
        statusLabel_ = new QLabel;
        statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusLabel_->setWordWrap(true);
        sl->addWidget(statusLabel_);
        col->addWidget(statusBox);

        // ---- configuration file ----
        auto *cfgBox = new QGroupBox(T("Configuration file"));
        auto *cl = new QVBoxLayout(cfgBox);
        auto *path = new QLabel(ctx.configPath);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        path->setWordWrap(true);
        cl->addWidget(path);
        auto *row = new QHBoxLayout;
        auto *open = new QPushButton(T("Open config"));
        open->setToolTip(T("Open the configuration file with the default application"));
        row->addWidget(open);
        row->addStretch(1);
        cl->addLayout(row);
        configMsg_ = makeNote();
        cl->addWidget(configMsg_);
        QObject::connect(open, &QPushButton::clicked, this, [this] {
            std::string err;
            if (ctx_.actions.openPath(ctx_.configPath.toUtf8().toStdString(), &err)) configMsg_->setText(QString());
            else configMsg_->setText(T("Could not open the file: ") + qs8(err));
        });
        col->addWidget(cfgBox);

        // ---- version ----
        auto *version = makeNote();
        version->setText(T("dofscroll %1").arg(QApplication::applicationVersion()));
        col->addWidget(version);
        col->addStretch(1);

        statusTimer_.setInterval(1000);
        QObject::connect(&statusTimer_, &QTimer::timeout, this, [this] { refreshStatus(); });
        refreshStatus();
        runAutostart(false, false);
    }
    ~GeneralPage() override {
        if (thread_) thread_->wait();  // the worker only reads/changes the autostart entries
    }

protected:
    void showEvent(QShowEvent *e) override {
        refreshStatus();
        statusTimer_.start();
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override {
        statusTimer_.stop();
        QWidget::hideEvent(e);
    }

private:
    QLabel *makeNote() {
        auto *l = new QLabel;
        l->setWordWrap(true);
        l->setForegroundRole(QPalette::PlaceholderText);
        return l;
    }

    void refreshStatus() {
        QStringList lines;
        for (const std::string &s : statusBlockLines(ctx_.status, ctx_.strings)) lines << qs8(s);
        statusLabel_->setText(lines.join(QLatin1Char('\n')));
    }

    // Reads (and first changes, if requested) the autostart state on a worker thread: the system
    // calls can take a moment (systemctl, registry, launchctl) and must not freeze the window.
    void runAutostart(bool change, bool enable) {
        if (busy_) return;
        busy_ = true;
        autostart_->setEnabled(false);
        autostartMsg_->setText(T("Working..."));
        ActionRunner *runner = &ctx_.actions;
        QThread *t = QThread::create([this, runner, change, enable] {
            AutoResult r;
            if (change) r.setOk = runner->setAutostart(enable, &r.setErr);
            r.state = spn::autostart::queryAutostart(&r.detail);
            QMetaObject::invokeMethod(this, [this, r] { onAutostartDone(r); }, Qt::QueuedConnection);
        });
        thread_ = t;
        QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
    }

    void onAutostartDone(const AutoResult &r) {
        busy_ = false;
        const AutostartView v = autostartView(r.state, r.detail, ctx_.strings);
        {
            QSignalBlocker b(autostart_);
            autostart_->setCheckState(v.partial ? Qt::PartiallyChecked : v.checked ? Qt::Checked : Qt::Unchecked);
        }
        autostart_->setEnabled(v.canToggle);
        autostart_->setToolTip(qs8(v.tooltip));
        QString text = qs8(v.text);
        if (!r.setOk) {
            const QString failed = T("Could not change the setting: ") + qs8(r.setErr);
            text = text.isEmpty() ? failed : failed + QLatin1Char('\n') + text;
        }
        autostartMsg_->setText(text);
        autostartMsg_->setForegroundRole(v.isError || !r.setOk ? QPalette::WindowText : QPalette::PlaceholderText);
    }

    void onLanguage() {
        UiSettings s;
        s.language = Language(language_->currentData().toInt());
        std::string err;
        if (saveUiSettings(uiSettingsPath(), s, &err)) {
            languageMsg_->setText(T("Takes effect the next time dofscroll-ui is started."));
            languageMsg_->setForegroundRole(QPalette::PlaceholderText);
        } else {
            languageMsg_->setText(T("Could not save the language: ") + qs8(err));
            languageMsg_->setForegroundRole(QPalette::WindowText);
        }
    }

    PageContext &ctx_;
    QCheckBox *autostart_ = nullptr;
    QLabel *autostartMsg_ = nullptr;
    QComboBox *language_ = nullptr;
    QLabel *languageMsg_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *configMsg_ = nullptr;
    QTimer statusTimer_;
    QPointer<QThread> thread_;
    bool busy_ = false;
};

}  // namespace

QWidget *createGeneralPage(PageContext &ctx) { return new GeneralPage(ctx); }
