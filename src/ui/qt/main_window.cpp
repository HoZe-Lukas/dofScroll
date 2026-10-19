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
#include "pages/profiles_page.hpp"
#include "platform/fs.hpp"
#include "ui/model/runtime_files.hpp"
#include "ui_i18n.hpp"

namespace {
enum { kRoleKind = Qt::UserRole };  // 0 = page entry, 1 = profile entry, 2 = group
enum { kRoleStackIndex = Qt::UserRole + 1, kRoleDefault = Qt::UserRole + 2 };
}  // namespace

MainWindow::MainWindow(const UiContext &base, QWidget *parent)
    : QMainWindow(parent), modes_(base.modesDir), ctx_(base) {
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
    if (!ctx_.reload && ctx_.runner) ctx_.reload = spn::ui::reloadVia(*ctx_.runner);
    if (!settings_.load(ctx_.configPath, &warnings, &err)) settings_.setProfiles(spn::ProfileSet(), ctx_.configPath);
    settings_.setReloadCallback(ctx_.reload);
    ctx_.settings = &settings_;
    ctx_.status = &status_;
    ctx_.modes = &modes_;
    ctx_.strings = &strings_;
    updateStatus();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { updateStatus(); });
    timer->start(1000);

    // Mode-independent pages from the registry: always available as their own entries.
    for (const PageFactory &f : pageFactories()) {
        auto *it = new QTreeWidgetItem(nav_, QStringList(T(f.title)));
        it->setData(0, kRoleKind, 0);
        QWidget *w = f.create(ctx_);
        if (std::string(f.id) == "profiles") profilesPage_ = w;
        it->setData(0, kRoleStackIndex, stack_->addWidget(w));
    }
    connect(nav_, &QTreeWidget::currentItemChanged, this, [this] { onSelectionChanged(); });
    setCentralWidget(split);
    nav_->setCurrentItem(nav_->topLevelItem(0));
}

// Reads the daemon's status file (bounded, owner checked) into the status model.
void MainWindow::updateStatus() {
    std::string text;
    const bool ok = spn::ui::readStatusFile(ctx_.runtimeDir, &text);  // the daemon's directory on every OS
    status_.update(text, ok, static_cast<int64_t>(std::time(nullptr)));
}

void MainWindow::bringToFront() {
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::onSelectionChanged() {
    QTreeWidgetItem *it = nav_->currentItem();
    if (!it) return;
    const int kind = it->data(0, kRoleKind).toInt();
    if (kind == 0)
        stack_->setCurrentIndex(it->data(0, kRoleStackIndex).toInt());
}

MainWindow::~MainWindow() { delete takeCentralWidget(); }

int MainWindow::pageCount() const { return nav_->topLevelItemCount(); }

int MainWindow::profileCount() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p ? p->profileCount() : 0;
}

bool MainWindow::profilePageHasModeSelector() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p && p->modeSelectorVisible();
}
