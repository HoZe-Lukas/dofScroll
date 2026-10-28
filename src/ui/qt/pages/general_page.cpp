// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "general_page.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <cstdlib>
#include <QComboBox>
#include <QSpinBox>
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
#include "driver_platform.hpp"
#include "driver_cleanup_dialog.hpp"
#include "log_dialog.hpp"
#include "theme.hpp"
#include "ui/model/channels_page_model.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/general_page_model.hpp"
#include "ui/model/input_path_model.hpp"
#include "ui/model/ui_links.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui_i18n.hpp"

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
    explicit GeneralPage(const UiContext &ctx) : QWidget(nullptr), ctx_(ctx) {
        // Two columns, so that the page fits the default window without scrolling: what you switch
        // and choose on the left, the daemon, the status and the files on the right.
        auto *col = new QVBoxLayout(this);
        using namespace spn::ui::tokens;
        col->setContentsMargins(kPageMargin, kSpaceLg, kPageMargin, kPageMargin);
        col->setSpacing(kSectionGap);
        auto *cols = new QHBoxLayout;
        cols->setSpacing(kSectionGap);
        auto *left = new QVBoxLayout, *right = new QVBoxLayout;
        left->setSpacing(kSectionGap);
        right->setSpacing(kSectionGap);
        cols->addLayout(left, 1);
        cols->addLayout(right, 1);
        col->addLayout(cols);

        // ---- output on/off ----
        auto *outBox = new QGroupBox(T("Output"));
        auto *ol = new QVBoxLayout(outBox);
        output_ = new QCheckBox(T("Send scroll, zoom and key events"));
        output_->setAccessibleName(T("Output"));
        output_->setToolTip(T("Turns everything dofscroll sends on or off. The tray's Enabled item is the same switch.") +
                            QStringLiteral("\n") + T("Config key: %1").arg(QStringLiteral("ENABLED")));
        ol->addWidget(output_);
        outputMsg_ = makeNote();
        outputMsg_->setText(T("Off: the 3D mouse does nothing through dofscroll (the vendor driver still works). A "
                              "program profile can switch output off for one program only."));
        ol->addWidget(outputMsg_);
        QObject::connect(output_, &QCheckBox::toggled, this, [this](bool on) {
            if (loadingModel_) return;
            std::string err;
            if (!ctx_.settings->setValue(0, "ENABLED", on ? "1" : "0", &err)) outputMsg_->setText(qs8(err));
            else ctx_.saveSoon();
        });
        left->addWidget(outBox);

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
        left->addWidget(loginBox);

        // ---- language and appearance: one box, so the page stays low at the default window size ----
        auto *lookBox = new QGroupBox(T("Language and appearance"));
        auto *lg = new QVBoxLayout(lookBox);
        auto *langLabel = new QLabel(T("Language"));
        langLabel->setTextFormat(Qt::PlainText);
        language_ = new QComboBox;
        langLabel->setBuddy(language_);
        language_->addItem(T("System default"), int(Language::System));
        language_->addItem(QStringLiteral("English"), int(Language::English));
        language_->addItem(QStringLiteral("Deutsch"), int(Language::German));
        language_->setAccessibleName(T("Language"));
        const Language cur = loadUiSettings(uiSettingsPath()).language;
        language_->setCurrentIndex(language_->findData(int(cur)));
        lg->addWidget(langLabel);
        lg->addWidget(language_);
        languageMsg_ = makeNote();
        languageMsg_->setText(T("Takes effect the next time dofscroll-ui is started."));
        lg->addWidget(languageMsg_);
        QObject::connect(language_, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onLanguage(); });

        auto *themeLabel = new QLabel(T("Appearance"));
        themeLabel->setTextFormat(Qt::PlainText);
        theme_ = new QComboBox;
        themeLabel->setBuddy(theme_);
        theme_->addItem(T("System default"), int(Theme::System));
        theme_->addItem(T("Light"), int(Theme::Light));
        theme_->addItem(T("Dark"), int(Theme::Dark));
        theme_->setAccessibleName(T("Theme"));
        theme_->setCurrentIndex(theme_->findData(int(loadUiSettings(uiSettingsPath()).theme)));
        lg->addWidget(themeLabel);
        lg->addWidget(theme_);
        themeMsg_ = makeNote();
        themeMsg_->setText(T("Applies at once and is remembered."));
        lg->addWidget(themeMsg_);
        QObject::connect(theme_, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onTheme(); });
        left->addWidget(lookBox);

        // ---- daemon (not part of a profile) ----
        auto *daemonBox = new QGroupBox(T("Daemon"));
        auto *dl = new QGridLayout(daemonBox);
        dl->setColumnStretch(1, 1);
        int drow = 0;
        tick_ = new QSpinBox;
        tick_->setRange(30, 1000);
        tick_->setKeyboardTracking(false);
        tick_->setAccessibleName(T("Tick rate (Hz)"));
        tick_->setToolTip(T("How many times per second the daemon generates events (30..1000)") + QStringLiteral("\n") +
                          T("Config key: %1").arg(QStringLiteral("TICK_HZ")));
        auto *tickLabel = new QLabel(T("Tick rate (Hz)"));
        tickLabel->setTextFormat(Qt::PlainText);
        tickLabel->setBuddy(tick_);
        dl->addWidget(tickLabel, drow, 0);
        dl->addWidget(tick_, drow, 1);
        ++drow;
        QObject::connect(tick_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            if (loadingModel_) return;
            std::string err;
            if (!ctx_.settings->setValue(0, "TICK_HZ", std::to_string(v), &err)) daemonMsg_->setText(qs8(err));
            else {
                daemonMsg_->setText(QString());
                ctx_.saveSoon();
            }
        });
#if !defined(_WIN32) && !defined(__APPLE__)  // X11/uinput exist on Linux only (Windows: SendInput, macOS: CGEvent)
        backend_ = new QComboBox;
        for (const char *id : {"auto", "x11", "uinput"}) backend_->addItem(QString::fromLatin1(id), QString::fromLatin1(id));
        backend_->setAccessibleName(T("Output backend"));
        backend_->setToolTip(T("auto: Wayland -> uinput, otherwise X11 (with fallback). Takes effect after the daemon is "
                               "restarted.") + QStringLiteral("\n") + T("Config key: %1").arg(QStringLiteral("BACKEND")));
        auto *beLabel = new QLabel(T("Output backend"));
        beLabel->setTextFormat(Qt::PlainText);
        beLabel->setBuddy(backend_);
        dl->addWidget(beLabel, drow, 0);
        dl->addWidget(backend_, drow, 1);
        ++drow;
        QObject::connect(backend_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            std::string err;
            if (!ctx_.settings->setValue(0, "BACKEND", backend_->currentData().toString().toUtf8().toStdString(), &err))
                daemonMsg_->setText(qs8(err));
            else {
                daemonMsg_->setText(QString());
                ctx_.saveSoon();
            }
        });
#endif
#ifdef DOFSCROLL_DRIVER_UI  // Linux reads through spacenavd: INPUT is ignored there, so there is no control
        input_ = new QComboBox;
        for (const InputChoice &c : inputChoices(*ctx_.strings)) input_->addItem(qs8(c.label), qs8(c.value));
        input_->setAccessibleName(T("Input path"));
        input_->setToolTip(qs8(inputExplanation(*ctx_.strings)) + QStringLiteral("\n") +
                           T("Config key: %1").arg(QStringLiteral("INPUT")));
        auto *inLabel = new QLabel(T("Input path"));
        inLabel->setTextFormat(Qt::PlainText);
        inLabel->setBuddy(input_);
        dl->addWidget(inLabel, drow, 0);
        dl->addWidget(input_, drow, 1);
        ++drow;
        QObject::connect(input_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            std::string err;
            if (!ctx_.settings->setValue(0, "INPUT", input_->currentData().toString().toUtf8().toStdString(), &err))
                daemonMsg_->setText(qs8(err));
            else {
                daemonMsg_->setText(QString());
                ctx_.saveSoon();
            }
        });
        inputActive_ = makeNote();
        inputActive_->setObjectName(QStringLiteral("input-active"));
        dl->addWidget(inputActive_, drow, 0, 1, 2);
        ++drow;
        auto *inputHelp = makeNote();
        inputHelp->setText(qs8(inputExplanation(*ctx_.strings)));
        dl->addWidget(inputHelp, drow, 0, 1, 2);
        ++drow;
#endif
        daemonMsg_ = makeNote();
        daemonMsg_->setText(T("These apply to the whole program, not to a profile. The output backend takes effect after "
                              "the daemon is restarted."));
        dl->addWidget(daemonMsg_, drow, 0, 1, 2);
        right->addWidget(daemonBox);

        // ---- status ----
        auto *statusBox = new QGroupBox(T("Status"));
        auto *sl = new QVBoxLayout(statusBox);
        statusLabel_ = new QLabel;
        statusLabel_->setTextFormat(Qt::PlainText);
        statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusLabel_->setWordWrap(true);
        sl->addWidget(statusLabel_);
        right->addWidget(statusBox);

        // ---- configuration file ----
        auto *cfgBox = new QGroupBox(T("Configuration file"));
        auto *cl = new QVBoxLayout(cfgBox);
        auto *path = new QLabel(qs8(ctx.configPath));
        path->setTextFormat(Qt::PlainText);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        path->setWordWrap(true);
        cl->addWidget(path);
        auto *row = new QHBoxLayout;
        auto *open = new QPushButton(T("Open config"));
        open->setToolTip(T("Open the configuration file with the default application"));
        row->addWidget(open);
        auto *log = new QPushButton(T("Show log..."));
        log->setObjectName(QStringLiteral("show-log"));
        log->setToolTip(T("Show the end of the daemon log"));
        row->addWidget(log);
        auto *docs = new QPushButton(T("Documentation"));
        docs->setObjectName(QStringLiteral("documentation"));
        docs->setToolTip(T("Open the documentation of the interface in your browser"));
        row->addWidget(docs);
        row->addStretch(1);
        cl->addLayout(row);
        configMsg_ = makeNote();
        cl->addWidget(configMsg_);
        QObject::connect(open, &QPushButton::clicked, this, [this] {
            std::string err;
            if (ctx_.runner->openPath(ctx_.configPath, &err)) configMsg_->setText(QString());
            else configMsg_->setText(T("Could not open the file: ") + qs8(err));
        });
        QObject::connect(log, &QPushButton::clicked, this, [this] {
            LogDialog::showFor(window(), ctx_.runtimeDir, *ctx_.strings);
        });
        QObject::connect(docs, &QPushButton::clicked, this, [this] {
            std::string err;
            if (spn::ui::openDocs(*ctx_.runner, "ui.md", &err)) configMsg_->setText(QString());
            else configMsg_->setText(T("Could not open the documentation: ") + qs8(err));
        });
        right->addWidget(cfgBox);

        // ---- the official 3Dconnexion driver (Windows: its per-program folder is known) ----
        const std::string driverDir = spn::ui::driverConfigDir();
        if (!driverDir.empty()) {
            auto *drvBox = new QGroupBox(T("3Dconnexion driver"));
            auto *dvl = new QVBoxLayout(drvBox);
            auto *drvNote = makeNote();
            drvNote->setText(T("If the driver has axes switched on for a program, that program receives every movement twice: "
                               "from the driver and from dofscroll. Switch them off here, except for your 3D and CAD programs."));
            dvl->addWidget(drvNote);
            auto *drvBtn = new QPushButton(T("Remove driver axis settings..."));
            drvBtn->setObjectName(QStringLiteral("driver-cleanup"));
            drvBtn->setToolTip(T("List the programs with active driver axes and switch off the ones you tick"));
            dvl->addWidget(drvBtn, 0, Qt::AlignLeft);
            QObject::connect(drvBtn, &QPushButton::clicked, this, [this, driverDir] {
                DriverCleanupDialog dlg(driverDir, window());
                dlg.exec();
            });
            right->addWidget(drvBox);
        }

        // ---- about: version, licences, and (Windows, macOS) what belongs to 3Dconnexion ----
        auto *aboutBox = new QGroupBox(T("About"));
        aboutBox->setObjectName(QStringLiteral("about-box"));
        auto *al = new QVBoxLayout(aboutBox);
#ifdef DOFSCROLL_DRIVER_UI
        constexpr bool kDriverPlatform = true;
#else
        constexpr bool kDriverPlatform = false;
#endif
        for (const std::string &line : aboutLines(QApplication::applicationVersion().toStdString(),
                                                  std::string(qVersion()), kDriverPlatform, *ctx_.strings)) {
            auto *l = makeNote();
            l->setText(qs8(line));
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            al->addWidget(l);
        }
        col->addWidget(aboutBox);
        left->addStretch(1);
        right->addStretch(1);
        col->addStretch(1);

        statusTimer_.setInterval(1000);
        QObject::connect(&statusTimer_, &QTimer::timeout, this, [this] {
            refreshStatus();
            refreshFromModel();
        });
        refreshStatus();
        refreshFromModel();
        runAutostart(false, false);
    }
    ~GeneralPage() override {
        if (thread_) thread_->wait();  // the worker only reads/changes the autostart entries
    }

protected:
    void showEvent(QShowEvent *e) override {
        refreshStatus();
        refreshFromModel();
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
        l->setTextFormat(Qt::PlainText);
        l->setWordWrap(true);
        l->setForegroundRole(QPalette::PlaceholderText);
        return l;
    }

    // The values of the configuration this page shows (the tray or another window may have changed
    // them); a control that has the keyboard focus is left alone.
    void refreshFromModel() {
        if (!ctx_.settings) return;
        loadingModel_ = true;
        spn::ui::SettingsModel::Value v;
        if (!output_->hasFocus() && ctx_.settings->value(0, "ENABLED", &v)) output_->setChecked(v.text != "0");
        if (!tick_->hasFocus() && ctx_.settings->value(0, "TICK_HZ", &v)) tick_->setValue(std::atoi(v.text.c_str()));
        if (backend_ && !backend_->hasFocus() && ctx_.settings->value(0, "BACKEND", &v)) {
            const int i = backend_->findData(qs8(v.text));
            backend_->setCurrentIndex(i < 0 ? 0 : i);
        }
        if (input_ && !input_->hasFocus() && ctx_.settings->value(0, "INPUT", &v))
            input_->setCurrentIndex(inputChoiceIndex(v.text));
        loadingModel_ = false;
    }

    void refreshStatus() {
        QStringList lines;
        for (const std::string &s : statusBlockLines(*ctx_.status, *ctx_.strings)) lines << qs8(s);
        statusLabel_->setText(lines.join(QLatin1Char('\n')));
        if (inputActive_) inputActive_->setText(qs8(inputActiveLine(*ctx_.status, *ctx_.strings)));
    }

    // Reads (and first changes, if requested) the autostart state on a worker thread: the system
    // calls can take a moment (systemctl, registry, launchctl) and must not freeze the window.
    void runAutostart(bool change, bool enable) {
        if (busy_) return;
        busy_ = true;
        autostart_->setEnabled(false);
        autostartMsg_->setText(T("Working..."));
        ActionRunner *runner = ctx_.runner;
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
        const AutostartView v = autostartView(r.state, r.detail, *ctx_.strings);
        {
            QSignalBlocker b(autostart_);
            autostart_->setCheckState(v.partial ? Qt::PartiallyChecked : v.checked ? Qt::Checked : Qt::Unchecked);
        }
        autostart_->setEnabled(v.canToggle);
        autostart_->setToolTip(qs8(v.tooltip));
        QString text = qs8(v.text);
        if (!r.setOk) {
            const QString failed = mark(spn::ui::StatusKind::Error, T("Could not change the setting: ") + qs8(r.setErr));
            text = text.isEmpty() ? failed : failed + QLatin1Char('\n') + text;
        } else if (v.isError && !text.isEmpty()) {
            text = mark(spn::ui::StatusKind::Warning, text);
        }
        setErrorLook(autostartMsg_, !r.setOk);
        autostartMsg_->setText(text);
        autostartMsg_->setForegroundRole(v.isError || !r.setOk ? QPalette::WindowText : QPalette::PlaceholderText);
    }

    // The red status colour of a failure message (the symbol and the words carry the meaning too).
    static void setErrorLook(QLabel *l, bool error) {
        l->setProperty("error", error);
        l->style()->unpolish(l);
        l->style()->polish(l);
    }

    void onTheme() {
        const Theme theme = Theme(theme_->currentData().toInt());
        applyTheme(theme);
        std::string err;
        if (updateUiSettings(uiSettingsPath(), [theme](UiSettings &s) { s.theme = theme; }, &err)) {
            themeMsg_->setText(T("Applies at once and is remembered."));
            themeMsg_->setForegroundRole(QPalette::PlaceholderText);
            setErrorLook(themeMsg_, false);
        } else {
            themeMsg_->setText(mark(spn::ui::StatusKind::Error, T("Could not save the theme: ") + qs8(err)));
            themeMsg_->setForegroundRole(QPalette::WindowText);
            setErrorLook(themeMsg_, true);
        }
    }

    void onLanguage() {
        const Language language = Language(language_->currentData().toInt());
        std::string err;
        if (updateUiSettings(uiSettingsPath(), [language](UiSettings &s) { s.language = language; }, &err)) {
            languageMsg_->setText(T("Takes effect the next time dofscroll-ui is started."));
            languageMsg_->setForegroundRole(QPalette::PlaceholderText);
            setErrorLook(languageMsg_, false);
        } else {
            languageMsg_->setText(mark(spn::ui::StatusKind::Error, T("Could not save the language: ") + qs8(err)));
            languageMsg_->setForegroundRole(QPalette::WindowText);
            setErrorLook(languageMsg_, true);
        }
    }

    UiContext ctx_;
    QCheckBox *output_ = nullptr;
    QLabel *outputMsg_ = nullptr, *daemonMsg_ = nullptr;
    QSpinBox *tick_ = nullptr;
    QComboBox *backend_ = nullptr, *input_ = nullptr;
    QLabel *inputActive_ = nullptr;
    bool loadingModel_ = false;
    QCheckBox *autostart_ = nullptr;
    QLabel *autostartMsg_ = nullptr;
    QComboBox *theme_ = nullptr;
    QLabel *themeMsg_ = nullptr;
    QComboBox *language_ = nullptr;
    QLabel *languageMsg_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *configMsg_ = nullptr;
    QTimer statusTimer_;
    QPointer<QThread> thread_;
    bool busy_ = false;
};

}  // namespace

QWidget *createGeneralPage(const UiContext &ctx) { return new GeneralPage(ctx); }
