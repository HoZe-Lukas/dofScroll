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
QString q(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
enum { kRoleName = Qt::UserRole };
}  // namespace

AddDeviceDialog::AddDeviceDialog(PageContext &ctx, const std::vector<std::string> &configured, QWidget *parent)
    : QDialog(parent), ctx_(ctx) {
    setWindowTitle(T("Add device"));
    resize(560, 440);
    model_.setConfigured(configured);

    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(T("Pick a connected device or type its name. The profile applies while that "
                              "device is used.")));
    auto *top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(T("Search by name or vendor:product"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(T("Search"));
    auto *refresh = new QPushButton(T("Refresh"), this);
    top->addWidget(search_, 1);
    top->addWidget(refresh);
    l->addLayout(top);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({T("Name"), T("Vendor:product"), T("Status")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    l->addWidget(list_, 1);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    l->addWidget(note_);

    l->addWidget(new QLabel(T("Device name:")));
    manual_ = new QLineEdit(this);
    manual_->setAccessibleName(T("Device name"));
    validation_ = new QLabel(this);
    validation_->setWordWrap(true);
    l->addWidget(manual_);
    l->addWidget(validation_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(T("Add"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    l->addWidget(buttons_);

    connect(search_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refresh(); });
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

    setTabOrder(search_, refresh);
    setTabOrder(refresh, list_);
    setTabOrder(list_, manual_);

    validate();
    refresh();
}

QString AddDeviceDialog::deviceName() const {
    std::string norm;
    if (validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, nullptr)) return q(norm);
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
        it->setText(0, q(r.dev.name));
        it->setText(1, q(r.idString));
        if (r.hasProfile) it->setText(2, T("Already has a profile"));
        it->setData(0, kRoleName, q(r.dev.name));
    }
}

void AddDeviceDialog::validate() {
    std::string norm, err;
    const bool ok = validateProfilePattern(manual_->text().toUtf8().toStdString(), &norm, &err);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
    validation_->setText(ok ? QString() : (manual_->text().isEmpty() ? QString() : T(err.c_str())));
}
