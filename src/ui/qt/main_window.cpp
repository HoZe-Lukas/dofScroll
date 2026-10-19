// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "main_window.hpp"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "config.hpp"
#include "modes.hpp"
#include "pages/profiles_page.hpp"
#include "platform_hooks.hpp"
#include "platform/running_apps.hpp"
#include "ui_i18n.hpp"

namespace {
enum { kRoleKind = Qt::UserRole };  // 0 = page entry, 1 = profile entry, 2 = group
enum { kRoleStackIndex = Qt::UserRole + 1, kRoleDefault = Qt::UserRole + 2 };
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("dofscroll"));
    resize(900, 520);
    std::vector<std::string> warnings;
    std::string err;
    const std::string cfg = spn::defaultConfigPath();
    if (!settings_.load(cfg, &warnings, &err)) settings_.setProfiles(spn::ProfileSet(), cfg);
    settings_.setModes(spn::loadModes(spn::modesDir(), nullptr), spn::modesDir());
    ctx_.settings = &settings_;
    ctx_.runner = &runner_;
    ctx_.runningApps = [] { return spn::makeRunningAppsProvider(); };
    ctx_.activeWindow = dofscroll_ui::makeActiveWindowPoll();

    auto *split = new QSplitter(this);
    nav_ = new QTreeWidget(split);
    nav_->setHeaderHidden(true);
    stack_ = new QStackedWidget(split);
    split->setStretchFactor(1, 1);

    // Mode-independent pages: always available as their own entries.
    const QStringList pages = {T("Channels"), T("Modes"), T("General"), T("Axes / assistant")};
    for (const QString &name : pages) {
        auto *it = new QTreeWidgetItem(nav_, QStringList(name));
        it->setData(0, kRoleKind, 0);
        QWidget *w = (name == T("Modes")) ? modesPage() : placeholder(name);
        it->setData(0, kRoleStackIndex, stack_->addWidget(w));
    }
    // Registered pages (see pages/page_registry.cpp): the Profiles page brings its own tree.
    for (const PageEntry &e : pageRegistry()) {
        auto *it = new QTreeWidgetItem(nav_, QStringList(T(e.title)));
        it->setData(0, kRoleKind, 0);
        QWidget *w = e.create(ctx_, stack_);
        if (std::string(e.id) == "profiles") profilesPage_ = w;
        it->setData(0, kRoleStackIndex, stack_->addWidget(w));
    }

    connect(nav_, &QTreeWidget::currentItemChanged, this, [this] { onSelectionChanged(); });
    setCentralWidget(split);
    nav_->setCurrentItem(nav_->topLevelItem(0));
}

QWidget *MainWindow::placeholder(const QString &title) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QStringLiteral("<b>") + title + QStringLiteral("</b>")));
    auto *todo = new QLabel(T("not implemented yet"));
    todo->setAlignment(Qt::AlignCenter);
    l->addWidget(todo, 1);
    return w;
}

// Thin placeholder: the list and the actions will be driven by the ModeCatalog model.
QWidget *MainWindow::modesPage() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QStringLiteral("<b>") + T("Modes") + QStringLiteral("</b>")));
    auto *hint = new QLabel(T("Mode files can press any key. Only use mode files from trusted "
                              "sources."));
    hint->setWordWrap(true);
    l->addWidget(hint);
    auto *row = new QHBoxLayout;
    auto *list = new QListWidget(w);  // built-in and file modes, warnings per mode
    list->addItem(T("not implemented yet"));
    row->addWidget(list, 1);
    auto *buttons = new QVBoxLayout;
    for (const QString &b : {T("New mode"), T("Duplicate"), T("Import file..."), T("Edit"),
                             T("Delete"), T("Open modes folder")})
        buttons->addWidget(new QPushButton(b, w));
    buttons->addStretch(1);
    row->addLayout(buttons);
    l->addLayout(row, 1);
    return w;
}

void MainWindow::onSelectionChanged() {
    QTreeWidgetItem *it = nav_->currentItem();
    if (!it) return;
    const int kind = it->data(0, kRoleKind).toInt();
    if (kind == 0)
        stack_->setCurrentIndex(it->data(0, kRoleStackIndex).toInt());
}

int MainWindow::pageCount() const { return nav_->topLevelItemCount(); }

int MainWindow::profileCount() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p ? p->profileCount() : 0;
}

bool MainWindow::profilePageHasModeSelector() const {
    const auto *p = qobject_cast<const ProfilesPage *>(profilesPage_);
    return p && p->modeSelectorVisible();
}
