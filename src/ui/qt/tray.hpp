// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tray icon with a placeholder menu (Open settings, Quit).
#pragma once
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class MainWindow;

class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *window, const QIcon &icon, QObject *parent = nullptr);
    void show();

private:
    QSystemTrayIcon icon_;
    QMenu menu_;
    MainWindow *window_;
};
