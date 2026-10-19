// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "main_window.hpp"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QTimer>
#include <ctime>

#include "common.hpp"
#include "config.hpp"
#include "pages/page_registry.hpp"
#include "platform/fs.hpp"
#include "ui_i18n.hpp"

namespace {
enum { kRoleKind = Qt::UserRole };  // 0 = page entry, 1 = profile entry, 2 = group
enum { kRoleStackIndex = Qt::UserRole + 1, kRoleDefault = Qt::UserRole + 2 };
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("dofscroll"));
    resize(900, 520);
    auto *split = new QSplitter(this);
    nav_ = new QTreeWidget(split);
    nav_->setHeaderHidden(true);
    stack_ = new QStackedWidget(split);
    split->setStretchFactor(1, 1);

    // Configuration, status and the page context (everything the pages share).
    std::vector<std::string> warnings;
    std::string err;
    const std::string cfg = spn::defaultConfigPath();
    if (!settings_.load(cfg, &warnings, &err)) settings_.setProfiles(spn::ProfileSet(), cfg);
    settings_.setReloadCallback([this](std::string *e) { return runner_.requestReload(e); });
    ctx_.reset(new PageContext{settings_, runner_, status_, strings_, QString::fromUtf8(cfg.data(), qsizetype(cfg.size()))});
    updateStatus();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { updateStatus(); });
    timer->start(1000);

    // Mode-independent pages from the registry: always available as their own entries.
    int n = 0;
    const PageInfo *pages = pageRegistry(&n);
    for (int i = 0; i < n; ++i) {
        auto *it = new QTreeWidgetItem(nav_, QStringList(T(pages[i].title)));
        it->setData(0, kRoleKind, 0);
        it->setData(0, kRoleStackIndex, stack_->addWidget(pages[i].create(*ctx_)));
    }
    // Profiles group: only what depends on the profile's mode lives below it.
    auto *group = new QTreeWidgetItem(nav_, QStringList(T("Profiles")));
    group->setData(0, kRoleKind, 2);
    struct Profile {
        const char *label;
        bool isDefault;
    };
    const Profile profiles[] = {{"Default mode", true},
                                {"Device profile (placeholder)", false},
                                {"Application profile (placeholder)", false}};
    for (const Profile &p : profiles) {
        auto *it = new QTreeWidgetItem(group, QStringList(T(p.label)));
        it->setData(0, kRoleKind, 1);
        it->setData(0, kRoleDefault, p.isDefault);
    }
    group->setExpanded(true);
    profilePage_ = new QWidget;
    stack_->addWidget(profilePage_);

    connect(nav_, &QTreeWidget::currentItemChanged, this, [this] { onSelectionChanged(); });
    setCentralWidget(split);
    nav_->setCurrentItem(nav_->topLevelItem(0));
}

// Reads the daemon's status file (bounded, owner checked) into the status model.
void MainWindow::updateStatus() {
    const std::string path = spn::statusFilePath();
    std::string text;
    bool ok = false;
    if (!path.empty()) {
        spn::fs::ReadOptions opt;
        opt.refuseLinks = true;
        opt.requireOwner = true;
        ok = spn::fs::readFileBounded(path, spn::kStatusMaxBytes, opt, &text).ok();
    }
    status_.update(text, ok, static_cast<int64_t>(std::time(nullptr)));
}

void MainWindow::onSelectionChanged() {
    QTreeWidgetItem *it = nav_->currentItem();
    if (!it) return;
    const int kind = it->data(0, kRoleKind).toInt();
    if (kind == 0)
        stack_->setCurrentIndex(it->data(0, kRoleStackIndex).toInt());
    else if (kind == 1)
        rebuildProfilePage(it);
}

// Model-driven rebuild: the content depends on the selected profile and its mode.
void MainWindow::rebuildProfilePage(QTreeWidgetItem *profile) {
    const bool isDefault = profile->data(0, kRoleDefault).toBool();
    auto *page = new QWidget;
    auto *l = new QVBoxLayout(page);
    modeSelectorVisible_ = !isDefault;
    if (!isDefault) {  // no mode selector for the default mode
        auto *combo = new QComboBox(page);
        combo->addItem(T("Default mode"));  // first entry, then the other modes
        l->addWidget(combo);
        connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                [this, profile] { rebuildProfilePage(profile); });
    }
    l->addWidget(new QLabel(T("Button bindings and channel actions: not implemented yet")));
    if (!isDefault) l->addWidget(new QPushButton(T("Edit mode file"), page));
    auto *ov = new QGroupBox(T("Overrides for this profile"), page);
    ov->setCheckable(true);
    ov->setChecked(false);  // off by default; the Channels page is the normal place
    l->addWidget(ov);
    l->addStretch(1);

    const int idx = stack_->indexOf(profilePage_);
    stack_->removeWidget(profilePage_);
    delete profilePage_;
    profilePage_ = page;
    stack_->insertWidget(idx, page);
    stack_->setCurrentWidget(page);
}

MainWindow::~MainWindow() { delete takeCentralWidget(); }

int MainWindow::pageCount() const { return nav_->topLevelItemCount(); }

int MainWindow::profileCount() const {
    const QTreeWidgetItem *g = nav_->topLevelItem(nav_->topLevelItemCount() - 1);
    return g ? g->childCount() : 0;
}

bool MainWindow::profilePageHasModeSelector() const { return modeSelectorVisible_; }
