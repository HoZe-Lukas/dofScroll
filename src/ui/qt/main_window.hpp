// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Settings window shell. Left: navigation tree (pages from the registry: Channels, General, Axes,
// Modes, and a "Profiles" group with Default mode + device/application profiles). Right: stacked area. The
// profile page is rebuilt whenever the selected profile or its mode changes.
#pragma once
#include <QMainWindow>
#include <memory>

#include "qt_action_runner.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "pages/page_registry.hpp"

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;  // destroys the pages before the models they reference
    int pageCount() const;                    // top-level navigation entries
    int profileCount() const;                 // entries below "Profiles"
    bool profilePageHasModeSelector() const;  // false for the default mode

private slots:
    void onSelectionChanged();

private:
    void updateStatus();
    void rebuildProfilePage(QTreeWidgetItem *profile);

    spn::ui::SettingsModel settings_;
    spn::ui::StatusModel status_;
    QtActionRunner runner_;
    QtStringProvider strings_;
    std::unique_ptr<PageContext> ctx_;
    QTreeWidget *nav_;
    QStackedWidget *stack_;
    QWidget *profilePage_ = nullptr;
    bool modeSelectorVisible_ = false;
};
