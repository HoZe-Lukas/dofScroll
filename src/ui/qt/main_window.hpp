// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Settings window shell. Left: vertical tabs for the categories (Profiles, Modes, Device,
// Settings; src/ui/model/navigation_model.hpp). Right: the selected category, with horizontal tabs
// for its pages when it has more than one (the pages come from the registry). Bottom: a status bar
// with the daemon state. The window owns the shared models (settings, status, modes) and completes
// the UiContext it gets from main() with them.
#pragma once
#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <string>
#include <vector>

#include "qt_action_runner.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/navigation_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui_context.hpp"

class QCloseEvent;
class QMoveEvent;
class QResizeEvent;
class QEvent;
class QLabel;
class QProxyStyle;
class QStackedWidget;
class QTabBar;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const UiContext &base, QWidget *parent = nullptr);
    ~MainWindow() override;  // destroys the pages before the models they reference
    void bringToFront();                      // show, un-minimize, raise and activate (every OS)
    // Before the application quits: asks Save / Discard / Cancel for unsaved edits (Modes and Button modes
    // pages). False: the user chose Cancel (or a save failed); do not quit.
    bool confirmQuit();
    int pageCount() const;                    // pages in all categories
    int categoryCount() const;                // left tabs
    // Selects a page by registry id ("channels", "axes", ...): its category and its tab. False if unknown.
    bool showPage(const QString &pageId);
    // Page ids in navigation order (category by category).
    std::vector<std::string> pageIds() const;
    // True if every left tab has an icon (false without the SVG module).
    bool categoryIconsLoaded() const;
    QIcon categoryIcon(int index) const;  // the icon of a left tab (null if there is none)
    int profileCount() const;                 // profiles listed on the Profiles page
    int overviewCardCount() const;            // profile cards on the Overview page
    int overviewColumns() const;              // columns of the card grid
    bool overviewCardsFit() const;
    bool overviewCardsFillWidth() const;            // no squashed card after a resize
    void openOverviewProfile(int section);    // what a click on an Overview card does (also used by the self-test)
    static QSize defaultWindowSize();  // 1280x820 on a large screen, smaller on a small one
    static QWidget *scrollable(QWidget *page);  // the page inside a scroll area (see main_window.cpp)
    QWidget *currentPage() const;             // the page that is shown (a tab of the category or its only page)
    QString currentPageId() const;            // registry id of the page that is shown
    bool selectedProfileIs(size_t section) const;  // is this profile selected on the Profiles page?
    bool profilePageHasModeSelector() const;  // false for the default mode

    // Window state: size, position and the selected page are kept in the per-user UI settings file
    // `settingsPath` (src/ui/model/ui_settings.hpp). This restores them now (a saved geometry that is
    // off all screens is ignored, the size respects the minimum) and from then on saves the page at
    // every change and everything when the window is closed. Without this call nothing is read or
    // written (the self-test windows).
    void enableStatePersistence(const std::string &settingsPath);
    bool restoreWindowState();  // false if the file holds no usable geometry
    void saveWindowState();     // geometry (only while visible and not minimized) and page
    // Is there a system tray? Without one, closing the window quits the application (an invisible
    // process would stay), and the status bar says so. Default: true.
    void setTrayAvailable(bool available);
    bool trayAvailable() const { return trayAvailable_; }
    QString trayNote() const;  // the status bar note about a missing tray ("" with a tray)

signals:
    // The window was closed and the process stays in the tray. Emitted on the first such close of a
    // run only; the tray shows a short balloon for it.
    void hiddenToTray();
    void quitRequested();  // the window was closed and there is no tray to keep the process useful

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;  // size and position are saved shortly after the last change
    void moveEvent(QMoveEvent *e) override;
    bool event(QEvent *e) override;  // a palette change redraws the icons and the tab style

private:
    QLabel *categoryTitle_ = nullptr;  // large title above the category
    void updateStatus();
    void savePage();
    void refreshAppearance();  // tab icons in the colour of the palette, tab style on top of the current style

    // Declared before ctx_ and the widgets: the pages reference them.
    spn::ui::SettingsModel settings_;
    spn::ui::StatusModel status_;
    spn::ui::ModeCatalog modes_;
    spn::ui::ButtonModeCatalog buttonModes_;
    QtStrings strings_;
    UiContext ctx_;
    std::vector<spn::ui::NavCategory> nav_;
    QTabBar *categoryBar_ = nullptr;
    QProxyStyle *tabStyle_ = nullptr;
    QStackedWidget *categories_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QWidget *profilesPage_ = nullptr;  // the "Profiles" page from the registry
    QWidget *overviewPage_ = nullptr;  // the "Overview" page from the registry
    QLabel *trayNote_ = nullptr;       // status bar note when there is no tray
    std::string statePath_;            // ui.conf the window state is kept in ("" = not kept)
    bool restoring_ = false;
    QTimer *stateTimer_ = nullptr;       // single shot: saves the geometry after a resize or move
    bool trayAvailable_ = true;
    bool trayNoticeShown_ = false;
};
