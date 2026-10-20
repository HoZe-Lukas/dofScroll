// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "button_row.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QToolButton>

#include <algorithm>

#include "key_combo.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

void addButtonRow(QObject *owner, QWidget *parent, QGridLayout *grid, int r, const BindingRow &row,
                  const ButtonRowEnv &env, const ButtonRowHandlers &handlers) {
    grid->addWidget(new QLabel(T("Button") + QStringLiteral(" %1").arg(row.number), parent), r, 0);
    if (row.beyondLimit || !row.valid) {
        grid->addWidget(new QLabel(qs8(row.text) + QStringLiteral("  ") +
                                       (row.beyondLimit ? T("(above this device's button count)")
                                                        : T("(not a valid action)")),
                                   parent),
                        r, 1, 1, 3);
        return;
    }
    const ButtonModeSet none;
    const std::vector<ButtonChoice> choices = buttonChoices(
        env.context, env.advancedOpen, env.buttonModes ? *env.buttonModes : none,
        row.binding.kind == BindingKind::ButtonMode ? row.binding.arg : std::string(), *env.strings);
    const int selected = selectedButtonChoice(choices, env.context, row);

    auto *kind = new QComboBox(parent);
    kind->setAccessibleName(T("Action of button") + QStringLiteral(" %1").arg(row.number));
    ButtonChoice::Group lastGroup = ButtonChoice::Group::None;
    for (size_t i = 0; i < choices.size(); ++i) {
        const ButtonChoice &c = choices[i];
        if (c.group != lastGroup && c.group != ButtonChoice::Group::None) {
            // Group header: a disabled entry that cannot be chosen.
            kind->addItem(c.group == ButtonChoice::Group::ButtonModes ? T("Button modes") : T("Actions"));
            auto *item = qobject_cast<QStandardItemModel *>(kind->model())->item(kind->count() - 1);
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
            kind->setItemData(kind->count() - 1, QVariant(), Qt::UserRole);
        }
        lastGroup = c.group;
        kind->addItem(qs8(c.label), int(i));
        if (!c.tooltip.empty()) kind->setItemData(kind->count() - 1, qs8(c.tooltip), Qt::ToolTipRole);
        if (int(i) == selected) kind->setCurrentIndex(kind->count() - 1);
    }
    if (selected < 0) kind->setCurrentIndex(-1);
    grid->addWidget(kind, r, 1);

    QWidget *arg = nullptr;
    const BindingKind bk = row.binding.kind;
    if (bk == BindingKind::Key) {
        auto *kc = makeKeyCombo(qs8(row.binding.arg), parent);
        kc->setAccessibleName(T("Key of button") + QStringLiteral(" %1").arg(row.number));
        QObject::connect(kc, &QComboBox::activated, owner, [handlers, kc](int) {
            if (!kc->currentData().isValid() || !handlers.setArgument) return;
            handlers.setArgument(Binding{BindingKind::Key, kc->currentData().toString().toUtf8().toStdString()});
        });
        arg = kc;
    } else if (bk == BindingKind::Mode || bk == BindingKind::ToggleMode || bk == BindingKind::HoldMode) {
        auto *mc = new QComboBox(parent);
        mc->setAccessibleName(T("Mode of button") + QStringLiteral(" %1").arg(row.number));
        std::vector<std::string> ids = env.modeIds;
        if (std::find(ids.begin(), ids.end(), row.binding.arg) == ids.end()) ids.push_back(row.binding.arg);
        for (const std::string &id : ids) mc->addItem(qs8(id), qs8(id));
        mc->setCurrentIndex(std::max(0, mc->findData(qs8(row.binding.arg))));
        QObject::connect(mc, &QComboBox::activated, owner, [handlers, mc, bk](int) {
            if (handlers.setArgument) handlers.setArgument(Binding{bk, mc->currentData().toString().toUtf8().toStdString()});
        });
        arg = mc;
    }
    if (!arg && env.showInherited && env.context == BindingContext::Specific && !row.overridden) {
        // Inherited: say what it is, so the effect of "Default (inherit)" is visible.
        auto *l = new QLabel(T("inherited:") + QLatin1Char(' ') + qs8(row.text), parent);
        l->setForegroundRole(QPalette::PlaceholderText);
        arg = l;
    }
    if (arg) grid->addWidget(arg, r, 2);

    QObject::connect(kind, &QComboBox::activated, owner, [handlers, kind, choices](int idx) {
        const QVariant data = kind->itemData(idx);
        if (!data.isValid()) return;  // a group header
        const size_t ci = size_t(data.toInt());
        if (ci < choices.size() && handlers.choose) handlers.choose(choices[ci]);
    });

    if (row.overridden) {
        auto *reset = new QToolButton(parent);
        reset->setText(env.resetText);
        reset->setToolTip(env.resetTip);
        QObject::connect(reset, &QToolButton::clicked, owner, [handlers] {
            if (handlers.reset) handlers.reset();
        });
        grid->addWidget(reset, r, 3);
    }
}
