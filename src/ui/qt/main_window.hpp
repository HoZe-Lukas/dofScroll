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
#include <vector>

#include "qt_action_runner.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/navigation_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui_context.hpp"

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
    QWidget *currentPage() const;             // the page that is shown (a tab of the category or its only page)
    QString currentPageId() const;            // registry id of the page that is shown
    bool selectedProfileIs(size_t section) const;  // is this profile selected on the Profiles page?
    bool profilePageHasModeSelector() const;  // false for the default mode

protected:
    bool event(QEvent *e) override;  // a palette change redraws the icons and the tab style

private:
    void updateStatus();
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
};
