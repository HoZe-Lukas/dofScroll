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

    // Mode-independent pages: always available as their own entries.
    const QStringList pages = {T("Channels"), T("Modes"), T("General"), T("Axes / assistant")};
    for (const QString &name : pages) {
        auto *it = new QTreeWidgetItem(nav_, QStringList(name));
        it->setData(0, kRoleKind, 0);
        QWidget *w = (name == T("Modes")) ? modesPage() : placeholder(name);
        it->setData(0, kRoleStackIndex, stack_->addWidget(w));
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

int MainWindow::pageCount() const { return nav_->topLevelItemCount(); }

int MainWindow::profileCount() const {
    const QTreeWidgetItem *g = nav_->topLevelItem(nav_->topLevelItemCount() - 1);
    return g ? g->childCount() : 0;
}

bool MainWindow::profilePageHasModeSelector() const { return modeSelectorVisible_; }
