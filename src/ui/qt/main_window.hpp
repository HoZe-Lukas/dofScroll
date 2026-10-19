// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Settings window shell (placeholders). Left: navigation tree (Channels, General, Axes, and a
// "Profiles" group with Default mode + device/application profiles). Right: stacked area. The
// profile page is rebuilt whenever the selected profile or its mode changes.
#pragma once
#include <QMainWindow>

#include "ui_context.hpp"

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const UiContext &ctx, QWidget *parent = nullptr);
    void bringToFront();                      // show, un-minimize, raise and activate (every OS)
    int pageCount() const;                    // top-level navigation entries
    int profileCount() const;                 // entries below "Profiles"
    bool profilePageHasModeSelector() const;  // false for the default mode

private slots:
    void onSelectionChanged();

private:
    QWidget *placeholder(const QString &title);
    void rebuildProfilePage(QTreeWidgetItem *profile);

    UiContext ctx_;
    QTreeWidget *nav_;
    QStackedWidget *stack_;
    QWidget *profilePage_ = nullptr;
    bool modeSelectorVisible_ = false;
};
