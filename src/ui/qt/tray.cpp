// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "tray.hpp"

#include <QAction>
#include <QApplication>

#include "main_window.hpp"
#include "ui_i18n.hpp"

Tray::Tray(MainWindow *window, const QIcon &icon, QObject *parent)
    : QObject(parent), icon_(icon), window_(window) {
    QAction *open = menu_.addAction(T("Open settings"));
    connect(open, &QAction::triggered, this, [this] {
        window_->show();
        window_->raise();
        window_->activateWindow();
    });
    menu_.addSeparator();
    QAction *quit = menu_.addAction(T("Quit"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);
    icon_.setContextMenu(&menu_);
    icon_.setToolTip(QStringLiteral("dofscroll"));
    connect(&icon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) window_->show();
            });
}

void Tray::show() { icon_.show(); }
