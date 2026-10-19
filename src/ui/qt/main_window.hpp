// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Settings window shell. Left: navigation tree (pages from the registry: Channels, General, Axes,
// Modes, and a "Profiles" group with Default mode + device/application profiles). Right: stacked
// area. The window owns the shared models (settings, status, modes) and completes the UiContext
// it gets from main() with them. The profile page is rebuilt whenever the selected profile or
// its mode changes.
#pragma once
#include <QMainWindow>

#include "qt_action_runner.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui_context.hpp"

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const UiContext &base, QWidget *parent = nullptr);
    ~MainWindow() override;  // destroys the pages before the models they reference
    void bringToFront();                      // show, un-minimize, raise and activate (every OS)
    int pageCount() const;                    // top-level navigation entries
    int profileCount() const;                 // entries below "Profiles"
    bool profilePageHasModeSelector() const;  // false for the default mode

private slots:
    void onSelectionChanged();

private:
    void updateStatus();

    // Declared before ctx_ and the widgets: the pages reference them.
    spn::ui::SettingsModel settings_;
    spn::ui::StatusModel status_;
    spn::ui::ModeCatalog modes_;
    QtStrings strings_;
    UiContext ctx_;
    QTreeWidget *nav_;
    QStackedWidget *stack_;
    QWidget *profilesPage_ = nullptr;  // the "Profiles" page from the registry
};
