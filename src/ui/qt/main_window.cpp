// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "main_window.hpp"
#include "ui/model/status_text.hpp"
#include "ui/model/first_device.hpp"
#include "ui/model/ui_progress.hpp"

#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QProxyStyle>
#include <QScreen>
#include <QStackedWidget>
#include <QStyle>
#include <QFileInfo>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>
#include <ctime>

#include "common.hpp"
#include "config.hpp"
#include "pages/page_registry.hpp"
#include "pages/button_modes_page.hpp"
#include "pages/modes_page.hpp"
#include "pages/overview_page.hpp"
#include "pages/profiles_page.hpp"
#include "pages/setup_guide_dialog.hpp"
#include "platform/fs.hpp"
#include "ui/model/runtime_files.hpp"
#include "theme.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui_i18n.hpp"
#include "vertical_tabs.hpp"

// First start: a roomy window, clamped to 90% of the screen, never below the minimum.
QSize MainWindow::defaultWindowSize() {
    QSize size(spn::ui::tokens::kDefaultWindowWidth, spn::ui::tokens::kDefaultWindowHeight);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        size = size.boundedTo(QSize(avail.width() * 9 / 10, avail.height() * 9 / 10));
    }
    return size.expandedTo(QSize(spn::ui::tokens::kMinWindowWidth, spn::ui::tokens::kMinWindowHeight));
}

MainWindow::MainWindow(const UiContext &base, QWidget *parent)
    : QMainWindow(parent),
      modes_(base.modesDir),
      buttonModes_(base.buttonModesDir),
      autosave_([this] { return settings_.dirty(); },
                [this] {
                    const spn::ui::SettingsModel::SaveResult r = settings_.save(strings_);
                    return spn::ui::Autosave::Outcome{r.saved, r.reloaded, r.error};
                },
                strings_),
      ctx_(base),
      guideFlow_(spn::ui::uiProgressPath(base.configPath)) {
    setWindowTitle(QStringLiteral("dofscroll"));
    resize(defaultWindowSize());  // the saved geometry (restoreWindowState) wins
    setMinimumSize(spn::ui::tokens::kMinWindowWidth, spn::ui::tokens::kMinWindowHeight);

    // Configuration, status and the page context (everything the pages share).
    std::vector<std::string> warnings;
    std::string err;
    if (!ctx_.reload && ctx_.runner) ctx_.reload = spn::ui::reloadVia(*ctx_.runner);
    if (!settings_.load(ctx_.configPath, &warnings, &err)) settings_.setProfiles(spn::ProfileSet(), ctx_.configPath);
    settings_.setReloadCallback(ctx_.reload);
    ctx_.settings = &settings_;
    ctx_.status = &status_;
    ctx_.modes = &modes_;
    ctx_.buttonModes = &buttonModes_;
    ctx_.strings = &strings_;
    ctx_.autosave = &autosave_;
    ctx_.notes = &startupNotes_;
    settings_.setModes(modes_.modeSet(), ctx_.modesDir);  // the joystick modes a profile can choose
    migrateLegacyButtons();

    // Left: one vertical tab per category. Right: the category (its pages as horizontal tabs).
    auto *central = new QWidget(this);
    auto *row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    categoryBar_ = new WestTabBar(central);
    categoryBar_->setShape(QTabBar::RoundedWest);
    categoryBar_->setExpanding(false);
    categoryBar_->setIconSize(QSize(spn::ui::tokens::kIconSize, spn::ui::tokens::kIconSize));
    categoryBar_->setDrawBase(false);
    categoryBar_->setAccessibleName(T("Categories"));
    categories_ = new QStackedWidget(central);
    // The sidebar: a slightly different surface with room around the navigation entries.
    auto *sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(spn::ui::tokens::kSpaceMd, spn::ui::tokens::kPageMargin, spn::ui::tokens::kSpaceMd,
                                   spn::ui::tokens::kSpaceMd);
    sideLayout->setSpacing(0);
    categoryBar_->setParent(sidebar);
    sideLayout->addWidget(categoryBar_, 0, Qt::AlignTop);
    sideLayout->addStretch(1);
    row->addWidget(sidebar);
    // Right: a large page title (the category), then the category.
    auto *content = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    categoryTitle_ = new QLabel(content);
    categoryTitle_->setProperty("heading", QStringLiteral("page"));
    categoryTitle_->setContentsMargins(spn::ui::tokens::kPageMargin, spn::ui::tokens::kSpaceLg, spn::ui::tokens::kPageMargin,
                                       spn::ui::tokens::kSpaceSm);
    categoryTitle_->setAccessibleName(T("Page title"));
    contentLayout->addWidget(categoryTitle_);
    contentLayout->addWidget(categories_, 1);
    row->addWidget(content, 1);

    std::vector<spn::ui::NavInput> in;
    for (const PageFactory &f : pageFactories()) in.push_back({f.id, f.title, f.category});
    std::vector<std::string> problems;
    nav_ = spn::ui::buildNavigation(in, &problems);
    for (const std::string &p : problems) fprintf(stderr, "dofscroll-ui: navigation: %s\n", p.c_str());

    for (const spn::ui::NavCategory &cat : nav_) {
        categoryBar_->addTab(T(cat.title.c_str()));
        QWidget *host = nullptr;
        auto *tabs = spn::ui::showTabBar(cat) ? new QTabWidget : nullptr;
        for (const spn::ui::NavPage &page : cat.pages) {
            for (const PageFactory &f : pageFactories()) {
                if (page.id != f.id) continue;
                QWidget *w = f.create(ctx_);
                if (page.id == "profiles") profilesPage_ = w;
                if (page.id == "overview") overviewPage_ = w;
                QWidget *shown = scrollable(w);
                if (tabs) tabs->addTab(shown, T(f.title));
                else host = shown;
            }
        }
        if (tabs) connect(tabs, &QTabWidget::currentChanged, this, [this] { savePage(); });
        categories_->addWidget(tabs ? static_cast<QWidget *>(tabs) : host);
    }
    connect(categoryBar_, &QTabBar::currentChanged, categories_, &QStackedWidget::setCurrentIndex);
    connect(categoryBar_, &QTabBar::currentChanged, this, [this](int i) {
        categoryTitle_->setText(categoryBar_->tabText(i));
    });
    connect(categoryBar_, &QTabBar::currentChanged, this, [this] { savePage(); });
    // The Overview opens other pages: a card selects its profile, the buttons show a page.
    if (auto *overview = qobject_cast<OverviewPage *>(overviewPage_)) {
        connect(overview, &OverviewPage::openProfile, this, [this](int section) { openOverviewProfile(section); });
        connect(overview, &OverviewPage::openPage, this, [this](const QString &id) {
            // "Tune scroll and zoom" opens the Channels tab of the Global profile
            if (id == QLatin1String("profile-channels")) openProfileTab(0, QStringLiteral("channels"));
            else showPage(id);
        });
        connect(overview, &OverviewPage::showSetupGuide, this, [this] { showSetupGuide(false); });
        // "New profile for the running program": the Profiles page opens its running-program picker.
        connect(overview, &OverviewPage::newProgramProfile, this, [this] {
            if (!showPage(QStringLiteral("profiles")) || !profilesPage_) return;
            QMetaObject::invokeMethod(profilesPage_, "addApplication", Qt::QueuedConnection);
        });
    }
    setCentralWidget(central);

    // Bottom: the daemon state, the same line as the tray's first entry.
    statusLabel_ = new QLabel(this);
    statusLabel_->setAccessibleName(T("Daemon status"));
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // a long state is cut, never squeezes the window
    statusBar()->addWidget(statusLabel_, 1);
    saveLabel_ = new QLabel(this);
    saveLabel_->setAccessibleName(T("Save status"));
    saveLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(saveLabel_);
    connect(&autosave_, &Autosaver::written, this, [this] { noteConfigStamp(); });
    connect(&autosave_, &Autosaver::statusChanged, this, [this](const QString &text, bool error) {
        // a symbol before the words, and the status colour: never colour alone
        const spn::ui::StatusKind kind = spn::ui::footerKind(text.toUtf8().toStdString(), error, strings_);
        saveLabel_->setText(text.isEmpty() ? text : qs8(spn::ui::withMark(kind, text.toUtf8().toStdString())));
        saveLabel_->setProperty("error", error);
        saveLabel_->setProperty("warning", !error && !text.isEmpty() && kind == spn::ui::StatusKind::Warning);
        saveLabel_->setProperty("success", !error && !text.isEmpty() && kind == spn::ui::StatusKind::Success);
        saveLabel_->style()->unpolish(saveLabel_);
        saveLabel_->style()->polish(saveLabel_);
    });

    noteConfigStamp();
    if (!startupNotes_.empty()) autosave_.report(qs8(startupNotes_.front()), false);  // the footer exists now
    updateStatus();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { updateStatus(); });
    timer->start(1000);
    refreshAppearance();  // the tab style and the icons
    categoryBar_->setCurrentIndex(0);
    categoryTitle_->setText(categoryBar_->tabText(0));
}

// The left tabs keep horizontal text through a proxy style on top of the current application style
// (so it is rebuilt when the theme changes the style), and their icons are drawn in the colour of
// the current palette.
void MainWindow::refreshAppearance() {
    QProxyStyle *style = new WestTabStyle(nullptr);
    style->setParent(categoryBar_);
    categoryBar_->setStyle(style);
    delete tabStyle_;
    tabStyle_ = style;
    const QColor color = palette().color(QPalette::ButtonText);
    for (size_t i = 0; i < nav_.size(); ++i)
        categoryBar_->setTabIcon(static_cast<int>(i),
                                 themedIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromStdString(nav_[i].id)), color,
                                            spn::ui::tokens::kIconSize));
}

bool MainWindow::event(QEvent *e) {
    const bool handled = QMainWindow::event(e);
    if (e->type() == QEvent::ApplicationPaletteChange && categoryBar_) refreshAppearance();
    return handled;
}

bool MainWindow::startSetupGuide(bool freshInstall) {
    if (freshInstall) {  // the profile of the first attached device is added once (updateStatus)
        const std::string pp = spn::ui::uiProgressPath(ctx_.configPath);
        spn::ui::UiProgress p = spn::ui::loadUiProgress(pp);
        p.firstDevicePending = true;
        std::string perr;
        spn::ui::saveUiProgress(pp, p, &perr);
    }
    if (!guideFlow_.start(freshInstall)) return false;
    showSetupGuide(true);
    return true;
}

void MainWindow::showSetupGuide(bool firstRun) {
    guideFlow_.show();
    if (!guide_) {
        guide_ = new SetupGuideDialog(ctx_, starter_, firstRun, this);
        guide_->setAttribute(Qt::WA_DeleteOnClose, false);
        // "Skip", "Finish" and the close button only remember that the guide was seen; nothing else is written
        auto remember = [this] {
            std::string err;
            if (!guideFlow_.skip(&err)) fprintf(stderr, "dofscroll-ui: cannot remember the setup guide: %s\n", err.c_str());
        };
        connect(guide_, &SetupGuideDialog::skipRequested, this, remember);
        connect(guide_, &SetupGuideDialog::finishRequested, this, remember);
    }
    guide_->show();
    guide_->raise();
    guide_->activateWindow();
}

bool MainWindow::setupGuideShown() const { return guide_ && guide_->isVisible(); }

QIcon MainWindow::categoryIcon(int index) const {
    return index >= 0 && index < categoryBar_->count() ? categoryBar_->tabIcon(index) : QIcon();
}

bool MainWindow::categoryIconsLoaded() const {
    for (int i = 0; i < categoryBar_->count(); ++i)
        if (categoryBar_->tabIcon(i).isNull()) return false;
    return categoryBar_->count() > 0;
}

// Reads the daemon's status file (bounded, owner checked) into the status model.
// The configuration file as the window last read or wrote it: size and modification time. The tray
// (Enabled) and a text editor change the file too; the window follows when it has nothing of its own
// to write.
void MainWindow::noteConfigStamp() {
    const QFileInfo fi(QString::fromStdString(ctx_.configPath));
    configStamp_ = fi.exists() ? QString::number(fi.size()) + QLatin1Char(':') + QString::number(fi.lastModified().toMSecsSinceEpoch())
                               : QString();
}

// An old configuration may still set the navigation or click button with NAV_BUTTON / CLICK_BUTTON. Those
// settings are moved to BUTTON_<n> entries (the Buttons tab shows them), the file is written and the daemon
// reloads it; what cannot be moved stays in the file and is listed as a note.
void MainWindow::migrateLegacyButtons() {
    if (!settings_.hasLegacyButtonKeys()) return;
    const spn::ui::SettingsModel::LegacyMigration r = settings_.migrateLegacyButtons(strings_);
    startupNotes_.clear();
    if (!r.migrated.empty()) {
        startupNotes_.push_back(strings_.tr("Moved the old navigation/click button settings to the Buttons tab"));
        for (const std::string &m : r.migrated) startupNotes_.push_back(" - " + m);
    }
    for (const std::string &n : r.notices) startupNotes_.push_back(strings_.tr("Not moved: ") + n);
    if (r.changed) {
        autosave_.requestImmediate();
        autosave_.flush();
    }
    if (!r.migrated.empty()) autosave_.report(qs8(startupNotes_.front()), false);
}

void MainWindow::followConfigFile() {
    if (autosave_.pending() || settings_.dirty()) return;  // our own edits are on their way: they win
    const QString before = configStamp_;
    noteConfigStamp();
    if (before == configStamp_) return;
    std::vector<std::string> warnings;
    std::string err;
    if (!settings_.load(ctx_.configPath, &warnings, &err)) return;  // keep what is shown
    settings_.setModes(modes_.modeSet(), ctx_.modesDir);
    migrateLegacyButtons();
    if (auto *p = qobject_cast<ProfilesPage *>(profilesPage_)) p->reloadFromModel();
}

void MainWindow::updateStatus() {
    followConfigFile();
    std::string text;
    const bool ok = spn::ui::readStatusFile(ctx_.runtimeDir, &text);  // the daemon's directory on every OS
    status_.update(text, ok, static_cast<int64_t>(std::time(nullptr)));
    if (statusLabel_) {
        const QString line = QString::fromStdString(status_.mainLine(strings_));
        statusLabel_->setText(line);
        statusLabel_->setToolTip(line);  // the full text when the window is too narrow for it
    }
    addFirstDeviceProfile();
    if (auto *overview = qobject_cast<OverviewPage *>(overviewPage_)) overview->refresh();
}

// New installation: the seeded configuration has no device profile; the first attached device gets one.
void MainWindow::addFirstDeviceProfile() {
    if (status_.status().device.empty() || autosave_.pending() || settings_.dirty()) return;
    const std::string pp = spn::ui::uiProgressPath(ctx_.configPath);
    spn::ui::UiProgress p = spn::ui::loadUiProgress(pp);
    if (!p.firstDevicePending) return;
    std::string err;
    if (spn::ui::addFirstDeviceProfile(settings_, status_.status().device, &err)) {
        ctx_.saveSoon();
        if (auto *pg = qobject_cast<ProfilesPage *>(profilesPage_)) pg->reloadFromModel();
    }
    p.firstDevicePending = false;  // once: also when the configuration already had a device profile
    spn::ui::saveUiProgress(pp, p, &err);
}

void MainWindow::bringToFront() {
    // A window that was hidden (closed to the tray, or never shown) comes back on the saved page.
    if (!statePath_.empty() && !isVisible()) {
        const std::string page = spn::ui::loadUiSettings(statePath_).page;
        if (!page.empty()) {
            restoring_ = true;
            showPage(QString::fromStdString(page));
            restoring_ = false;
        }
    }
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

bool MainWindow::flushPending() { return Autosaver::flushAll(); }

QString MainWindow::saveStatus() const { return saveLabel_ ? saveLabel_->text() : QString(); }

bool MainWindow::showPage(const QString &pageId) {
    const std::string id = pageId.toStdString();
    for (size_t c = 0; c < nav_.size(); ++c) {
        for (size_t p = 0; p < nav_[c].pages.size(); ++p) {
            if (nav_[c].pages[p].id != id) continue;
            categoryBar_->setCurrentIndex(static_cast<int>(c));
            if (auto *tabs = qobject_cast<QTabWidget *>(categories_->widget(static_cast<int>(c))))
                tabs->setCurrentIndex(static_cast<int>(p));
            return true;
        }
    }
    return false;
}

std::vector<std::string> MainWindow::pageIds() const {
    std::vector<std::string> ids;
    for (const spn::ui::NavCategory &c : nav_)
        for (const spn::ui::NavPage &p : c.pages) ids.push_back(p.id);
    return ids;
}

MainWindow::~MainWindow() { delete takeCentralWidget(); }

int MainWindow::pageCount() const { return static_cast<int>(spn::ui::navPageCount(nav_)); }

int MainWindow::categoryCount() const { return static_cast<int>(nav_.size()); }

bool MainWindow::openProfileTab(size_t section, const QString &tab) {
    if (!showPage(QStringLiteral("profiles"))) return false;
    auto *p = qobject_cast<ProfilesPage *>(profilesPage_);
    if (!p) return false;
    const bool ok = p->selectSection(section);
    return p->selectTab(tab) && ok;
}

ProfilesPage *MainWindow::profilesPage() const { return qobject_cast<ProfilesPage *>(profilesPage_); }

void MainWindow::openOverviewProfile(int section) {
    showPage(QStringLiteral("profiles"));
    if (auto *p = qobject_cast<ProfilesPage *>(profilesPage_)) p->selectSection(static_cast<size_t>(section));
}

bool MainWindow::overviewCardsFillWidth() const {
    const auto *o = qobject_cast<const OverviewPage *>(overviewPage_);
    return o && o->cardsFillWidth();
}

int MainWindow::overviewColumns() const {
    const auto *o = qobject_cast<const OverviewPage *>(overviewPage_);
    return o ? o->cardColumns() : 0;
}

bool MainWindow::overviewCardsFit() const {
    const auto *o = qobject_cast<const OverviewPage *>(overviewPage_);
    return o && o->cardsFit();
}

int MainWindow::overviewCardCount() const {
    const auto *o = qobject_cast<const OverviewPage *>(overviewPage_);
    return o ? o->profileCardCount() : 0;
}

QWidget *MainWindow::currentPage() const {
    const int c = categoryBar_->currentIndex();
    QWidget *w = c >= 0 ? categories_->widget(c) : nullptr;
    if (auto *tabs = qobject_cast<QTabWidget *>(w)) w = tabs->currentWidget();
    if (w && w->objectName() == QLatin1String("pageScroll")) return static_cast<QScrollArea *>(w)->widget();
    return w;
}

// Every page sits in a scroll area: in a small window the page keeps the size its content needs
// (text stays readable, controls are not squashed) and scrolls instead; scrolling horizontally
// only happens when even the minimum width does not fit.
QWidget *MainWindow::scrollable(QWidget *page) {
    auto *area = new QScrollArea;
    area->setObjectName(QStringLiteral("pageScroll"));
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setWidget(page);
    return area;
}

QString MainWindow::currentPageId() const {
    const int c = categoryBar_->currentIndex();
    if (c < 0 || c >= static_cast<int>(nav_.size())) return QString();
    int p = 0;
    if (const auto *tabs = qobject_cast<const QTabWidget *>(categories_->widget(c))) p = tabs->currentIndex();
    const auto &pages = nav_[static_cast<size_t>(c)].pages;
    return p >= 0 && p < static_cast<int>(pages.size()) ? QString::fromStdString(pages[static_cast<size_t>(p)].id) : QString();
}

bool MainWindow::selectedProfileIs(size_t section) const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p && p->selectedSection() == section;
}

int MainWindow::profileCount() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p ? p->profileCount() : 0;
}


void MainWindow::enableStatePersistence(const std::string &settingsPath) {
    statePath_ = settingsPath;
    if (!stateTimer_) {
        stateTimer_ = new QTimer(this);
        stateTimer_->setSingleShot(true);
        stateTimer_->setInterval(800);
        connect(stateTimer_, &QTimer::timeout, this, [this] { saveWindowState(); });
    }
    restoreWindowState();
}

// The window can stay open for days (or only be hidden to the tray): the geometry is not kept until
// the close, it is saved a moment after the last resize or move.
void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (stateTimer_ && isVisible() && !restoring_) stateTimer_->start();
}

void MainWindow::moveEvent(QMoveEvent *e) {
    QMainWindow::moveEvent(e);
    if (stateTimer_ && isVisible() && !restoring_) stateTimer_->start();
}

bool MainWindow::restoreWindowState() {
    if (statePath_.empty()) return false;
    const spn::ui::UiSettings saved = spn::ui::loadUiSettings(statePath_);
    if (!saved.page.empty()) {
        restoring_ = true;
        showPage(QString::fromStdString(saved.page));  // an id that no longer exists is ignored
        restoring_ = false;
    }
    if (!saved.window.valid) return false;
    std::vector<spn::ui::ScreenRect> screens;
    for (const QScreen *s : QGuiApplication::screens()) {
        const QRect r = s->availableGeometry();
        screens.push_back({r.x(), r.y(), r.width(), r.height()});
    }
    const spn::ui::WindowGeometry g = spn::ui::withMinimumSize(saved.window, spn::ui::tokens::kMinWindowWidth,
                                                               spn::ui::tokens::kMinWindowHeight);
    if (!spn::ui::geometryVisibleOn(g, screens)) return false;  // off all screens (a screen was removed)
    resize(g.w, g.h);
    move(g.x, g.y);  // outer frame position, as saved
    return true;
}

void MainWindow::savePage() {
    flushPending();  // leaving a page or a tab: its last edit is written first
    if (statePath_.empty() || restoring_) return;
    const std::string page = currentPageId().toStdString();
    if (page.empty()) return;
    std::string err;
    if (!spn::ui::updateUiSettings(statePath_, [&page](spn::ui::UiSettings &s) { s.page = page; }, &err))
        fprintf(stderr, "dofscroll-ui: cannot save the window state: %s\n", err.c_str());
}

void MainWindow::saveWindowState() {
    if (statePath_.empty()) return;
    const std::string page = currentPageId().toStdString();
    // A window that was never shown, or is minimized, has no geometry worth keeping.
    const bool geometry = isVisible() && !isMinimized();
    spn::ui::WindowGeometry g;
    if (geometry) {
        const bool normal = !(isMaximized() || isFullScreen());
        const QPoint p = normal ? pos() : normalGeometry().topLeft();
        const QSize sz = normal ? size() : normalGeometry().size();
        g.valid = true;
        g.x = p.x();
        g.y = p.y();
        g.w = sz.width();
        g.h = sz.height();
    }
    std::string err;
    if (!spn::ui::updateUiSettings(
            statePath_,
            [&](spn::ui::UiSettings &s) {
                if (g.valid) s.window = g;
                if (!page.empty()) s.page = page;
            },
            &err))
        fprintf(stderr, "dofscroll-ui: cannot save the window state: %s\n", err.c_str());
}

void MainWindow::closeEvent(QCloseEvent *e) {
    flushPending();  // the last edit is on disk before the window goes away
    saveWindowState();
    e->accept();  // hides the window; the process lives on in the tray
    if (qGuiApp->isSavingSession()) return;
    if (!trayAvailable_) {
        emit quitRequested();
    } else if (!trayNoticeShown_) {
        trayNoticeShown_ = true;
        emit hiddenToTray();
    }
}

void MainWindow::setTrayAvailable(bool available) {
    trayAvailable_ = available;
    if (available) {
        delete trayNote_;
        trayNote_ = nullptr;
    } else if (!trayNote_) {
        trayNote_ = new QLabel(T("No system tray found: closing the window quits dofscroll-ui"), this);
        trayNote_->setAccessibleName(T("System tray note"));
        statusBar()->addPermanentWidget(trayNote_);
    }
}

QString MainWindow::trayNote() const { return trayNote_ ? trayNote_->text() : QString(); }
