// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tray icon: a thin view on spn::ui::TrayController. The menu is rendered from the controller's
// TrayMenu (status lines, Mode submenu with radio items, Enabled toggle, open actions, Quit), the
// icon shows enabled / disabled / daemon not running, the tooltip the active profile and mode.
// A one second timer refreshes everything.
#pragma once
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QStringList>
#include <QSystemTrayIcon>

#include "qt_action_runner.hpp"
#include "ui/model/tray_controller.hpp"
#include "ui_context.hpp"

class QTimer;
class MainWindow;

class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *window, const QIcon &baseIcon, const UiContext &ctx, QObject *parent = nullptr);
    void show();
    int modeMenuCount() const;   // radio items in the Mode submenu
    // Menu text of a label from a file or the daemon: "&" is doubled (a QMenu reads a single one as a mnemonic).
    static QString menuText(const std::string &text);
    int menuEntryCount() const;  // visible top-level actions and submenus
    QStringList modeMenuToolTips() const;  // the tooltip of each entry of the joystick mode submenu (self-test)
    spn::ui::TrayController &controller() { return controller_; }

private slots:
    void refresh();

private:
    void rebuild();
    void addEntry(QMenu *into, const spn::ui::TrayEntry &e);
    void applyState();
    void run(const spn::ui::TrayEntry &e);  // perform + report a failure + refresh afterwards

    QtStrings strings_;
    spn::ui::TrayController controller_;
    QSystemTrayIcon icon_;
    QMenu menu_;
    MainWindow *window_;
    QIcon enabledIcon_, disabledIcon_, notRunningIcon_;
    QTimer *timer_;
};
