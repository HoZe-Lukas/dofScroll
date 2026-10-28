// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Profiles" page: the list of profiles on the left (Global first, then the devices, then the
// programs) and the editor of the selected profile on the right. The editor always says which
// profile it edits (icon, name and kind in the header) and has the same tabs for every profile:
// General (joystick mode, enabled), Channels (scroll and zoom, see channels_editor.hpp), Motions and
// buttons (the picture, with every button row in its inspector) and Overrides (advanced). Switching the profile keeps the tab. Every change is saved by itself
// (UiContext::autosave); there is no Save or Revert button and nothing asks about unsaved changes.
// All decisions come from the models in src/ui/model/.
#pragma once
#include <QWidget>

class QSplitter;

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QStringList>

#include "button_row.hpp"
#include "button_modes.hpp"
#include "config.hpp"
#include "modes.hpp"
#include "live_feed/live_feed.hpp"
#include "profiles_env.hpp"
#include "ui/model/device_source_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui/model/profile_import.hpp"
#include "ui/model/profile_tree_model.hpp"
#include "ui/model/profile_undo.hpp"
#include "ui_context.hpp"

class ChannelsEditor;
class ProfileMotionsView;
class QEvent;
class QGridLayout;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QTimer;
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

    // Tabs of the selected profile: general, channels, motions, overrides (the same for every profile).
    QStringList tabIds() const;
    QString currentTabId() const;
    bool selectTab(const QString &id);
    // Opens the Motions and buttons tab with button n selected (its row is in the inspector).
    bool selectButton(int n);
    // The header of the editor: which profile is edited.
    bool tabVisible(const QString &id) const;  // self-test: is the tab shown (hidden while the output is off)
    QString headerName() const;  // "Global", the device name or the program name
    QString headerKind() const;  // "Global profile", "Device profile" or "Program profile"
    QString headerNote() const;  // the one line about inheritance below the name
    bool headerHasIcon() const;
    double editorShare() const;  // self-test: width of the editor side / width of the splitter
    QStringList profileListLabels() const;  // the list as shown: Global, group headers and profiles in order
    QString treeLabel(size_t section) const;  // the label of a profile in the list (name and number of overrides)
    ProfileMotionsView *motionsView() const { return motions_; }
    ChannelsEditor *channelsEditor() const { return channels_; }
    // Search boxes: the profile list (names) and the settings rows of the selected profile.
    void setProfileFilter(const QString &text);
    void setSettingsFilter(const QString &text);
    int visibleProfileCount() const;  // profiles shown in the list (Global not counted)
    int settingRowCount() const;      // rows shown in the tabs of the selected profile
    // Export of the selected profile (its section as a .conf file) without a dialog; false + lastError().
    bool exportSelected(const QString &file);
    const QString &lastError() const { return lastError_; }
    // The actions of the profile list without a dialog (the buttons call them after their dialogs; the self-test
    // calls them directly). All of them go through the settings model and the autosave.
    bool moveSelected(bool up);                           // Move up / Move down in the order of the profiles of its kind
    bool removeSelected();                                // Remove (the button asks first); Undo stays offered until something else changes (no time limit)
    bool undoRemove();                                    // Undo of the last removal (the profile with all its settings)
    bool undoAvailable() const { return removed_.valid; }
    void dismissUndo() { clearRemovedUndo(); }
    bool renameSelected(const QString &name);             // Change application / device: the new name, same settings
    bool importFrom(const QString &file, bool replaceOnCollision);  // Import; a name that exists is replaced or kept
    QString defectNotes() const;                          // the words about defective profile files under the list
    QString orderNote() const;                            // the order of the selected profile in words (General tab)
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
    void newProfile();  // the profile builder (profile_builder_dialog.hpp)
    void renameProfile();
    void removeProfile();
    void exportProfile();
    void importProfile();

private:
    void rebuildTree(size_t selectSection);
    void clearRemovedUndo();
    void applyProfileFilter();
    bool settingMatches(const QString &label, const std::string &key) const;
    void rebuildContent();
    void rebuildHeader();
    void refreshTreeLabels();  // the numbers of overrides in the list
    void setTabContent(QScrollArea *area, const QList<QWidget *> &parts);
    size_t currentSection() const;
    void refreshDeviceLimit(size_t section);
    void queueRebuild();
    void pollDeviceFeed();
    QWidget *buildModeSelector(size_t section);
    QWidget *buildEnabled(size_t section);
    // Change application / device, Export and Remove of the selected profile (General tab).
    QWidget *buildProfileActions(size_t section);
    // Notes about moved old button settings and the Global "Advanced" section: shown above the picture of the Motions and buttons tab.
    QList<QWidget *> buildButtonExtras(size_t section, bool bindingEditor);
    void fillBindingOrigin(spn::ui::BindingRow &row, size_t section);
    struct BindingKit {  // what a button row needs while it is built
        spn::ModeSet modes;
        spn::ButtonModeSet buttonModes;
        std::vector<std::string> offered;
    };
    ButtonRowEnv makeBindingEnv(size_t section, BindingKit *kit);
    void addBindingRow(QWidget *parent, QGridLayout *grid, int gridRow, size_t section, const spn::ui::BindingRow &row,
                       const ButtonRowEnv &env, const std::vector<std::string> &offered);
    void addBindingRowFor(QWidget *parent, QGridLayout *grid, int gridRow, size_t section, int n);
    QWidget *buildModeInfo(size_t section);
    QWidget *buildOverrides(size_t section);
    void problem(const QString &text);  // a message for the status bar
    bool doImport(const QString &file, const std::function<int(const spn::ui::ImportPlan &)> &ask);  // 1 go on / replace, 0 keep the existing, -1 cancel
    QString defectText(const spn::ui::ProfileEntry &e) const;
    const spn::ui::StringProvider &strings() const;

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
    QSplitter *split_ = nullptr;
    QScrollArea *general_ = nullptr, *overrides_ = nullptr;
    ProfileMotionsView *motions_ = nullptr;
    ChannelsEditor *channels_ = nullptr;
    QPushButton *add_app_, *add_dev_, *import_, *new_profile_;
    QLabel *defectNote_ = nullptr;  // defective profile files, in words, under the list
    QWidget *undoBar_ = nullptr;    // "Profile removed. Undo" under the list, no time limit
    QLabel *undoLabel_ = nullptr;
    bool keepUndo_ = false;  // rebuildTree() drops the offer, except while a removal itself rebuilds it
    spn::ui::RemovedProfile removed_;
    size_t shown_ = 0;  // section the tabs show
    int settingRows_ = 0;
    bool advancedOpen_ = false;
    bool rebuildQueued_ = false;
    QString lastError_;
};

QWidget *createProfilesPage(const UiContext &ctx);
