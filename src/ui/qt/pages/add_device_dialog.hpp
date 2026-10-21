// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Add device" dialog: the connected devices (DevicePickerModel) plus a manual name entry.
#pragma once
#include <QDialog>
#include <string>
#include <vector>

#include "profiles_env.hpp"
#include "ui/model/app_picker_model.hpp"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

class AddDeviceDialog : public QDialog {
    Q_OBJECT
public:
    // current (not empty): "Change device" for an existing profile; the name starts as `current` and `configured`
    // must not contain it.
    AddDeviceDialog(const ProfilesEnv &ctx, const std::vector<std::string> &configured, QWidget *parent = nullptr,
                    const QString &current = QString());
    QString deviceName() const;  // validated, trimmed

private:
    void refresh();
    void refill();
    void validate();

    const ProfilesEnv &ctx_;
    spn::DevicePickerModel model_;
    QLineEdit *search_, *manual_;
    QTreeWidget *list_;
    QLabel *note_, *validation_;
    QDialogButtonBox *buttons_;
};
