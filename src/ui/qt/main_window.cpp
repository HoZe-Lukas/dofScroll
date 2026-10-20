// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "main_window.hpp"

#include <QHBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QProxyStyle>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>
#include <ctime>

#include "common.hpp"
#include "config.hpp"
#include "pages/page_registry.hpp"
#include "pages/overview_page.hpp"
#include "pages/profiles_page.hpp"
#include "platform/fs.hpp"
#include "ui/model/runtime_files.hpp"
#include "theme.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui_i18n.hpp"
#include "vertical_tabs.hpp"

MainWindow::MainWindow(const UiContext &base, QWidget *parent)
    : QMainWindow(parent), modes_(base.modesDir), buttonModes_(base.buttonModesDir), ctx_(base) {
    setWindowTitle(QStringLiteral("dofscroll"));
    resize(960, 560);
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

    // Left: one vertical tab per category. Right: the category (its pages as horizontal tabs).
    auto *central = new QWidget(this);
    auto *row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    categoryBar_ = new QTabBar(central);
    categoryBar_->setShape(QTabBar::RoundedWest);
    categoryBar_->setExpanding(false);
    categoryBar_->setIconSize(QSize(spn::ui::tokens::kIconSize, spn::ui::tokens::kIconSize));
    categoryBar_->setDrawBase(false);
    categoryBar_->setAccessibleName(T("Categories"));
    categories_ = new QStackedWidget(central);
    row->addWidget(categoryBar_, 0, Qt::AlignTop);
    row->addWidget(categories_, 1);

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
                if (tabs) tabs->addTab(w, T(f.title));
                else host = w;
            }
        }
        categories_->addWidget(tabs ? static_cast<QWidget *>(tabs) : host);
    }
    connect(categoryBar_, &QTabBar::currentChanged, categories_, &QStackedWidget::setCurrentIndex);
    // The Overview opens other pages: a card selects its profile, the buttons show a page.
    if (auto *overview = qobject_cast<OverviewPage *>(overviewPage_)) {
        connect(overview, &OverviewPage::openProfile, this, [this](int section) { openOverviewProfile(section); });
        connect(overview, &OverviewPage::openPage, this, [this](const QString &id) { showPage(id); });
    }
    setCentralWidget(central);

    // Bottom: the daemon state, the same line as the tray's first entry.
    statusLabel_ = new QLabel(this);
    statusLabel_->setAccessibleName(T("Daemon status"));
    statusBar()->addWidget(statusLabel_, 1);

    updateStatus();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { updateStatus(); });
    timer->start(1000);
    refreshAppearance();  // the tab style and the icons
    categoryBar_->setCurrentIndex(0);
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

QIcon MainWindow::categoryIcon(int index) const {
    return index >= 0 && index < categoryBar_->count() ? categoryBar_->tabIcon(index) : QIcon();
}

bool MainWindow::categoryIconsLoaded() const {
    for (int i = 0; i < categoryBar_->count(); ++i)
        if (categoryBar_->tabIcon(i).isNull()) return false;
    return categoryBar_->count() > 0;
}

// Reads the daemon's status file (bounded, owner checked) into the status model.
void MainWindow::updateStatus() {
    std::string text;
    const bool ok = spn::ui::readStatusFile(ctx_.runtimeDir, &text);  // the daemon's directory on every OS
    status_.update(text, ok, static_cast<int64_t>(std::time(nullptr)));
    if (statusLabel_) statusLabel_->setText(QString::fromStdString(status_.mainLine(strings_)));
    if (auto *overview = qobject_cast<OverviewPage *>(overviewPage_)) overview->refresh();
}

void MainWindow::bringToFront() {
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

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
    if (auto *tabs = qobject_cast<QTabWidget *>(w)) return tabs->currentWidget();
    return w;
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

bool MainWindow::profilePageHasModeSelector() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p && p->modeSelectorVisible();
}
