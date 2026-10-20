// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Profiles" page: tree (Global, device profiles, application profiles) on the left and the
// profile page on the right. The right side is rebuilt whenever the selected profile or its
// effective mode changes. All decisions come from the models in src/ui/model/.
#pragma once
#include <QElapsedTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QPointer>
#include <QStringList>

#include "config.hpp"
#include "live_feed/live_feed.hpp"
#include "profiles_env.hpp"
#include "ui/model/device_source_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui_context.hpp"

class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Answer of the "unsaved changes" question (Save, Discard, Cancel).
enum class UnsavedChoice { Save, Discard, Cancel };
// Asks the question; `what` names the profile, empty for "the configuration". Tests replace it.
using UnsavedPrompt = std::function<UnsavedChoice(const QString &what)>;

class ProfilesPage : public QWidget {
    Q_OBJECT
public:
    explicit ProfilesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int profileCount() const;          // tree entries below the default mode
    bool modeSelectorVisible() const;  // false for the default mode
    // Selects the profile of a settings section (a click on an Overview card). False if there is none.
    bool selectSection(size_t section);
    size_t selectedSection() const { return currentSection(); }

    // Sub-tabs of the selected profile: general, channels, buttons, overrides (those that apply).
    QStringList tabIds() const;
    QString currentTabId() const;
    bool selectTab(const QString &id);
    // Search boxes: the profile list (names) and the settings rows of the selected profile.
    void setProfileFilter(const QString &text);
    void setSettingsFilter(const QString &text);
    int visibleProfileCount() const;  // profiles shown in the list (default mode not counted)
    int settingRowCount() const { return settingRows_; }  // rows built in the tabs of the selected profile
    // Revert: discards the unsaved edits of the selected profile.
    bool revertSelected();
    bool selectedModified() const;
    bool revertEnabled() const;
    bool saveEnabled() const;
    // Question for unsaved changes (Save/Discard/Cancel); the default is a message box.
    void setUnsavedPrompt(UnsavedPrompt p) { prompt_ = std::move(p); }
    // The guard used when another profile or page is shown and when the window closes: true if
    // it is fine to continue (nothing unsaved, or saved/discarded). wholeFile: any unsaved edit.
    bool resolveUnsaved(bool wholeFile);

protected:
    void hideEvent(QHideEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void onSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void revert();
    void addApplication();
    void addDevice();
    void renameProfile();
    void removeProfile();
    void save();

private:
    void rebuildTree(size_t selectSection);
    void rebuildTreeByName(spn::Section::Kind kind, const std::string &pattern);
    void applyProfileFilter();
    void guardLeave();
    void returnToPage();
    QString shownName() const;
    bool settingMatches(const QString &label, const std::string &key) const;
    void rebuildContent();
    void setStatus(const QString &text);
    void updateButtons();
    size_t currentSection() const;
    void refreshDeviceLimit(size_t section);
    void queueRebuild();
    void pollDeviceFeed();
    QWidget *buildModeSelector(size_t section);
    QWidget *buildBindings(size_t section);
    QWidget *buildButtonSettings(size_t section);
    QWidget *buildModeInfo(size_t section);
    QWidget *buildOverrides(size_t section);
    QWidget *buildEffective(size_t section);

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
    QTabWidget *tabs_;
    QLabel *status_;
    QPushButton *add_app_, *add_dev_, *rename_, *remove_, *save_, *revert_;
    UnsavedPrompt prompt_;
    QPointer<QWidget> filtered_;  // the window whose close event is guarded
    QString lastTab_ = QStringLiteral("general");  // remembered for the session
    size_t shown_ = 0;       // section the tabs show
    size_t builtSection_ = static_cast<size_t>(-1);  // section the tabs were built for last
    int settingRows_ = 0;
    bool leaving_ = false;
    bool advancedOpen_ = false;
    bool modeSelectorVisible_ = false;
    bool rebuildQueued_ = false;
};

QWidget *createProfilesPage(const UiContext &ctx);
