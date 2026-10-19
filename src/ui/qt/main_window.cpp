// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "main_window.hpp"

#include <QLabel>
#include <QStringList>
#include <QTabWidget>

#include "ui_i18n.hpp"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("dofscroll"));
    resize(720, 480);
    auto *tabs = new QTabWidget(this);
    const QString todo = T("not implemented yet");
    const QStringList names = {T("General"), T("Channels"), T("Buttons"),
                               T("Modes"),   T("Profiles"), T("Axes")};
    for (const QString &name : names) {
        auto *page = new QLabel(todo, tabs);
        page->setAlignment(Qt::AlignCenter);
        tabs->addTab(page, name);
    }
    setCentralWidget(tabs);
}

int MainWindow::pageCount() const {
    const auto *t = qobject_cast<const QTabWidget *>(centralWidget());
    return t ? t->count() : 0;
}
