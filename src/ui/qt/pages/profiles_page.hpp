// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Profiles" page: the list of profiles on the left (Global first, then the devices, then the
// programs) and the editor of the selected profile on the right. The editor always says which
// profile it edits (icon, name and kind in the header) and has the same tabs for every profile:
// General (joystick mode, enabled), Channels (scroll and zoom, see channels_editor.hpp), Buttons
// and Overrides (advanced). Switching the profile keeps the tab. Every change is saved by itself
// (UiContext::autosave); there is no Save or Revert button and nothing asks about unsaved changes.
// All decisions come from the models in src/ui/model/.
#pragma once
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QPointer>
#include <QStringList>

#include "config.hpp"
#include "live_feed/live_feed.hpp"
#include "profiles_env.hpp"
#include "ui/model/device_source_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui_context.hpp"

class ChannelsEditor;
class QEvent;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

class ProfilesPage : public QWidget {
    Q_OBJECT
public:
    explicit ProfilesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int profileCount() const;  // tree entries below Global
    // Selects the profile of a settings section (a click on an Overview card). False if there is none.
    bool selectSection(size_t section);
    size_t selectedSection() const { return currentSection(); }

    // Tabs of the selected profile: general, channels, buttons, overrides (the same for every profile).
    QStringList tabIds() const;
    QString currentTabId() const;
    bool selectTab(const QString &id);
    // The header of the editor: which profile is edited.
    QString headerName() const;  // "Global", the device name or the program name
    QString headerKind() const;  // "Global profile", "Device profile" or "Program profile"
    QString headerNote() const;  // the one line about inheritance below the name
    bool headerHasIcon() const;
    QStringList profileListLabels() const;  // the list as shown: Global, group headers and profiles in order
    QString treeLabel(size_t section) const;  // the label of a profile in the list (name and number of overrides)
    ChannelsEditor *channelsEditor() const { return channels_; }
    // Search boxes: the profile list (names) and the settings rows of the selected profile.
    void setProfileFilter(const QString &text);
    void setSettingsFilter(const QString &text);
    int visibleProfileCount() const;  // profiles shown in the list (Global not counted)
    int settingRowCount() const;      // rows shown in the tabs of the selected profile
    // Export of the selected profile (its section as a .conf file) without a dialog; false + lastError().
    bool exportSelected(const QString &file);
    const QString &lastError() const { return lastError_; }
    // The model changed behind the page (the configuration file was edited elsewhere): show it again.
    void reloadFromModel() { rebuildTree(currentSection()); }

protected:
    void hideEvent(QHideEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private slots:
    void onSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void addApplication();
    void addDevice();
    void renameProfile();
    void removeProfile();
    void exportProfile();

private:
    void rebuildTree(size_t selectSection);
    void applyProfileFilter();
    bool settingMatches(const QString &label, const std::string &key) const;
    void rebuildContent();
    void rebuildHeader();
    void refreshTreeLabels();  // the numbers of overrides in the list
    void setTabContent(QScrollArea *area, const QList<QWidget *> &parts);
    void updateButtons();
    size_t currentSection() const;
    void refreshDeviceLimit(size_t section);
    void queueRebuild();
    void pollDeviceFeed();
    QWidget *buildModeSelector(size_t section);
    QWidget *buildEnabled(size_t section);
    QWidget *buildBindings(size_t section);
    QWidget *buildModeInfo(size_t section);
    QWidget *buildOverrides(size_t section);
    void problem(const QString &text);  // a message for the status bar

    const UiContext &ctx_;
    ProfilesEnv env_;
    // Device list: the live feed's device info frame, with the status file's device= line as
    // fallback while the feed is unavailable (see spn::ui::DeviceSourceModel).
    std::unique_ptr<spn::LiveFeedClient> feedClient_;
    spn::LiveFeedModel feedModel_;
    spn::ui::DeviceSourceModel devices_;
    QElapsedTimer feedClock_;
    QTreeWidget *tree_;
    QLineEdit *profileFilter_, *settingsFilter_;
    QLabel *headerIcon_ = nullptr, *headerName_ = nullptr, *headerKind_ = nullptr, *headerNote_ = nullptr;
    QTabWidget *tabs_;
    QScrollArea *general_ = nullptr, *buttons_ = nullptr, *overrides_ = nullptr;
    ChannelsEditor *channels_ = nullptr;
    QPushButton *add_app_, *add_dev_, *rename_, *remove_, *export_;
    size_t shown_ = 0;  // section the tabs show
    int settingRows_ = 0;
    bool advancedOpen_ = false;
    bool rebuildQueued_ = false;
    QString lastError_;
};

QWidget *createProfilesPage(const UiContext &ctx);
