// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Settings window shell (placeholders). Left: navigation tree (Channels, General, Axes, and a
// "Profiles" group with Default mode + device/application profiles). Right: stacked area. The
// profile page is rebuilt whenever the selected profile or its mode changes.
#pragma once
#include <QMainWindow>

#include <memory>

#include "pages/page_registry.hpp"
#include "qt_action_runner.hpp"
#include "ui/model/settings_model.hpp"

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    int pageCount() const;                    // top-level navigation entries
    int profileCount() const;                 // entries below "Profiles"
    bool profilePageHasModeSelector() const;  // false for the default mode

private slots:
    void onSelectionChanged();

private:
    QWidget *modesPage();
    QWidget *placeholder(const QString &title);

    QTreeWidget *nav_;
    QStackedWidget *stack_;
    spn::ui::SettingsModel settings_;
    QtActionRunner runner_;
    PageContext ctx_;
    QWidget *profilesPage_ = nullptr;
};
