// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Profiles" page: tree (Default mode, device profiles, application profiles) on the left and the
// profile page on the right. The right side is rebuilt whenever the selected profile or its
// effective mode changes. All decisions come from the models in src/ui/model/.
#pragma once
#include <QWidget>

#include "profiles_env.hpp"
#include "ui_context.hpp"

class QLabel;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

class ProfilesPage : public QWidget {
    Q_OBJECT
public:
    explicit ProfilesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int profileCount() const;          // tree entries below the default mode
    bool modeSelectorVisible() const;  // false for the default mode

private slots:
    void onSelectionChanged();
    void addApplication();
    void addDevice();
    void renameProfile();
    void removeProfile();
    void save();

private:
    void rebuildTree(size_t selectSection);
    void rebuildContent();
    void setStatus(const QString &text);
    void updateButtons();
    size_t currentSection() const;
    void refreshDeviceLimit(size_t section);
    void queueRebuild();
    QWidget *buildModeSelector(size_t section);
    QWidget *buildBindings(size_t section);
    QWidget *buildButtonSettings(size_t section);
    QWidget *buildModeInfo(size_t section);
    QWidget *buildOverrides(size_t section);

    const UiContext &ctx_;
    ProfilesEnv env_;
    QTreeWidget *tree_;
    QScrollArea *scroll_;
    QLabel *status_;
    QPushButton *add_app_, *add_dev_, *rename_, *remove_, *save_;
    bool advancedOpen_ = false;
    bool modeSelectorVisible_ = false;
    bool rebuildQueued_ = false;
};

QWidget *createProfilesPage(const UiContext &ctx);
