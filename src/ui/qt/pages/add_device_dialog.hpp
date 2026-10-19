// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Add device" dialog: the connected devices (DevicePickerModel) plus a manual name entry.
#pragma once
#include <QDialog>
#include <string>
#include <vector>

#include "page_registry.hpp"
#include "ui/model/app_picker_model.hpp"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

class AddDeviceDialog : public QDialog {
    Q_OBJECT
public:
    AddDeviceDialog(PageContext &ctx, const std::vector<std::string> &configured, QWidget *parent = nullptr);
    QString deviceName() const;  // validated, trimmed

private:
    void refresh();
    void refill();
    void validate();

    PageContext &ctx_;
    spn::DevicePickerModel model_;
    QLineEdit *search_, *manual_;
    QTreeWidget *list_;
    QLabel *note_, *validation_;
    QDialogButtonBox *buttons_;
};
