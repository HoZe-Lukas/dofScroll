// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "button_row.hpp"

#include <memory>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "key_combo.hpp"
#include "motion_view.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui_i18n.hpp"
#include "plain_text.hpp"

using namespace spn;
using namespace spn::ui;

void addButtonRow(QObject *owner, QWidget *parent, QGridLayout *grid, int r, const BindingRow &row,
                  const ButtonRowEnv &env, const ButtonRowHandlers &handlers) {
    auto *numberLabel = new QLabel(T("Button") + QStringLiteral(" %1").arg(row.number), parent);
    if (env.deviceButtons > 0) numberLabel->setToolTip(T("This device has %1 buttons").arg(env.deviceButtons));
    grid->addWidget(numberLabel, r, 0);
    if (row.beyondLimit || !row.valid) {
        auto *bad = new QLabel(qs8(row.text) + QStringLiteral("  ") +
                                   (row.beyondLimit ? T("(above this device's button count)") : T("(not a valid action)")),
                               parent);
        bad->setTextFormat(Qt::PlainText);  // the action text comes from the configuration file
        grid->addWidget(bad, r, 1, 1, 3);
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
        if (!c.tooltip.empty()) kind->setItemData(kind->count() - 1, spn::ui::qt::plainToolTip(qs8(c.tooltip)), Qt::ToolTipRole);
        if (int(i) == selected) kind->setCurrentIndex(kind->count() - 1);
    }
    if (selected < 0) kind->setCurrentIndex(-1);
    // The drop-down with the shared button chip under it ("1 Space", muted "N Unassigned"): the same chip as on
    // the other pages. Under the drop-down, so the row does not get wider and the chip wraps with the font size.
    // The page rebuilds after every change, so the chip always shows the effective action.
    auto *cell = new QWidget(parent);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(2);
    cellLayout->addWidget(kind);
    auto *chip = new motionview::ButtonChips(cell);
    chip->setObjectName(QStringLiteral("buttonChip"));
    chip->setButtons({spn::ui::buttonChip(row.number, row.text, std::string(), *env.strings)}, QString());
    chip->setAccessibleName(T("Chip of button") + QStringLiteral(" %1").arg(row.number));
    cellLayout->addWidget(chip);
    grid->addWidget(cell, r, 1);

    QWidget *arg = nullptr;
    const BindingKind bk = row.binding.kind;
    if (bk == BindingKind::Key) {
        // modifier boxes plus the key: "key:ctrl+0" is a chord that is tapped once per press
        auto chord = std::make_shared<std::string>(row.binding.arg);
        arg = chordPicker(parent, owner, chord.get(), T("Key of button") + QStringLiteral(" %1").arg(row.number),
                          [handlers, chord] {
                              if (handlers.setArgument) handlers.setArgument(Binding{BindingKind::Key, *chord});
                          });
    } else if (bk == BindingKind::Mode || bk == BindingKind::ToggleMode || bk == BindingKind::HoldMode) {
        auto *mc = new QComboBox(parent);
        mc->setAccessibleName(T("Joystick mode of button") + QStringLiteral(" %1").arg(row.number));
        std::vector<std::string> ids = env.modeIds;
        if (std::find(ids.begin(), ids.end(), row.binding.arg) == ids.end()) ids.push_back(row.binding.arg);
        for (const std::string &id : ids) {
            mc->addItem(qs8(id), qs8(id));
            auto tip = env.modeTips.find(id);
            if (tip != env.modeTips.end()) mc->setItemData(mc->count() - 1, spn::ui::qt::plainToolTip(qs8(tip->second)), Qt::ToolTipRole);
        }
        mc->setCurrentIndex(std::max(0, mc->findData(qs8(row.binding.arg))));
        QObject::connect(mc, &QComboBox::activated, owner, [handlers, mc, bk](int) {
            if (handlers.setArgument) handlers.setArgument(Binding{bk, mc->currentData().toString().toUtf8().toStdString()});
        });
        arg = mc;
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
    // Where the action comes from: "Inherited from Global: key:space", "Set in this profile", or in [global]
    // the profiles that override it. Written out, never only a colour.
    if (!row.origin.empty()) {
        auto *origin = new QLabel(qs8(row.origin), parent);
        origin->setTextFormat(Qt::PlainText);  // names profiles and joystick modes
        origin->setForegroundRole(QPalette::PlaceholderText);
        origin->setWordWrap(true);
        origin->setMinimumWidth(180);
        origin->setMaximumWidth(320);
        origin->setAccessibleName(T("Origin of button") + QStringLiteral(" %1").arg(row.number));
        if (!row.originTip.empty()) spn::ui::qt::setPlainToolTip(origin, qs8(row.originTip));
        grid->addWidget(origin, r, 4);
    }
}
