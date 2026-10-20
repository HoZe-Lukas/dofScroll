// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key chooser shared by the pages: grouped list, group names are disabled header rows.
#pragma once
#include <QComboBox>
#include <QStandardItemModel>

#include "ui/model/key_list.hpp"
#include "ui_i18n.hpp"

inline QComboBox *makeKeyCombo(const QString &current, QWidget *parent) {
    auto *c = new QComboBox(parent);
    c->setMaxVisibleItems(20);
    auto *model = qobject_cast<QStandardItemModel *>(c->model());
    for (const spn::ui::KeyGroup &g : spn::ui::keyGroups()) {
        c->addItem(T(g.title.c_str()));
        if (model) model->item(c->count() - 1)->setFlags(Qt::NoItemFlags);
        for (const std::string &k : g.keys) c->addItem(QString::fromStdString(k), QString::fromStdString(k));
    }
    const int i = c->findData(current);
    if (i >= 0) c->setCurrentIndex(i);
    return c;
}
