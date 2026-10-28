// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "add_device_dialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui_i18n.hpp"

using namespace spn;

namespace {
enum { kRoleName = Qt::UserRole };
}  // namespace

AddDeviceDialog::AddDeviceDialog(const ProfilesEnv &ctx, const std::vector<std::string> &configured, QWidget *parent,
                                 const QString &current)
    : QDialog(parent), ctx_(ctx) {
    const bool change = !current.isEmpty();
    change_ = change;
    configured_ = configured;
    setWindowTitle(change ? T("Change device") : T("Add device"));
    resize(640, 500);
    model_.setConfigured(configured);

    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(change ? T("Pick the device this profile should apply to now, or type its name. The "
                                        "settings of the profile stay.")
                                    : T("Pick a connected device or type its name. The profile applies while that "
                                        "device is used."));
    intro->setTextFormat(Qt::PlainText);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(T("Search by name or vendor:product"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(T("Search"));
    auto *refreshBtn = new QPushButton(T("Refresh"), this);
    top->addWidget(search_, 1);
    top->addWidget(refreshBtn);
    l->addLayout(top);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setAccessibleName(T("Connected devices"));
    list_->setHeaderLabels({T("Name"), T("Vendor:product"), T("Status")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    l->addWidget(list_, 1);
    note_ = new QLabel(this);
    note_->setTextFormat(Qt::PlainText);
    note_->setWordWrap(true);
    l->addWidget(note_);

    l->addWidget(new QLabel(T("Device name:")));
    manual_ = new QLineEdit(this);
    manual_->setAccessibleName(T("Device name"));
    validation_ = new QLabel(this);
    validation_->setTextFormat(Qt::PlainText);
    validation_->setWordWrap(true);
    l->addWidget(manual_);
    l->addWidget(validation_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(change ? T("Change") : T("Add"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    l->addWidget(buttons_);

    connect(search_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] {
        const QTreeWidgetItem *it = list_->currentItem();
        if (it) manual_->setText(it->data(0, kRoleName).toString());
    });
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (buttons_->button(QDialogButtonBox::Ok)->isEnabled()) accept();
    });
    connect(manual_, &QLineEdit::textChanged, this, [this] { validate(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setTabOrder(search_, refreshBtn);
    setTabOrder(refreshBtn, list_);
    setTabOrder(list_, manual_);

    if (change) manual_->setText(current);
    validate();
    refresh();
}

QString AddDeviceDialog::deviceName() const {
    std::string norm;
    if (validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, nullptr)) return qs8(norm);
    return QString();
}

// A device snapshot is cheap (no I/O of its own), so it is read on the UI thread.
void AddDeviceDialog::refresh() {
    std::vector<DeviceInfo> devs;
    if (ctx_.devices) devs = ctx_.devices();
    note_->setText(devs.empty() ? T("No connected device was reported. You can still type the device name.")
                                : QString());
    model_.setDevices(std::move(devs));
    refill();
}

void AddDeviceDialog::refill() {
    list_->clear();
    for (const DeviceRow &r : model_.rows(search_->text().toUtf8().toStdString())) {
        auto *it = new QTreeWidgetItem(list_);
        it->setText(0, qs8(r.dev.name));
        it->setText(1, qs8(r.idString));
        if (r.hasProfile) it->setText(2, T("Already has a profile"));
        it->setData(0, kRoleName, qs8(r.dev.name));
    }
}

void AddDeviceDialog::validate() {
    std::string norm, err;
    const bool valid = validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, &err);
    const bool taken = valid && hasProfileFor(configured_, norm);
    const bool ok = valid && !(change_ && taken);  // changing to a device that has a profile would fail
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
    if (valid) validation_->setText(change_ && taken ? T("This device already has a profile.") : QString());
    else validation_->setText(manual_->text().isEmpty() ? QString() : T(err.c_str()));
}
