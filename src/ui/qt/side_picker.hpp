// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// One side of a step: / hold: action (stage 4 of #224): modifier check boxes plus the key tree picker, with the Mouse >
// Buttons branch switched on, so the side can be a key, a key combination or a mouse click. Like chordPicker (key_combo.hpp),
// but narrow: the check boxes sit on a line above the picker, so an inspector of 400 pixels needs no horizontal scroll bar.
// A mouse button takes no modifier (the check boxes are greyed and cleared while one is chosen); "none" greys the whole side.
#pragma once
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "key_combo.hpp"
#include "key_tree_picker.hpp"
#include "ui/model/key_tree_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui_i18n.hpp"

// side: "ctrl+pageup", "none", "click", ... (edited in place). accessible: what the side is ("Pushing forward, key or click").
// *picker (optional) receives the key picker, the widget a label takes as its buddy. Object names for tests: sidePickerBox,
// sidePickerKey, sidePickerMod_<name>.
inline QWidget *sidePicker(QWidget *parent, QObject *owner, std::string *side, const QString &accessible,
                           const std::function<void()> &changed, KeyTreePicker **picker = nullptr, bool mouse = true) {
    spn::ui::KeyTreeOptions opt;
    opt.mouse = mouse;  // false for the analog keys:, which never take a click
    auto *w = new QWidget(parent);
    w->setObjectName(QStringLiteral("sidePickerBox"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto *mods = new QGridLayout;  // two by two: four boxes in a row are wider than a narrow inspector with a wide font
    mods->setContentsMargins(0, 0, 0, 0);
    std::vector<std::string> sel;
    std::string key;
    splitChord(*side, &sel, &key);
    const bool none = *side == "none";
    const bool isMouse = spn::ui::ChannelForm::isMouseWord(*side);
    std::vector<QCheckBox *> boxes;
    for (const char *m : kModNames) {
        const QString label = modifierLabel(m);
        auto *cb = new QCheckBox(label, w);
        cb->setObjectName(QStringLiteral("sidePickerMod_") + QString::fromLatin1(m));
        cb->setProperty("chordMod", QString::fromLatin1(m));  // the stored name; the text is for the people
        cb->setProperty("look", QStringLiteral("box"));
        cb->setChecked(!none && !isMouse && std::find(sel.begin(), sel.end(), m) != sel.end());
        cb->setEnabled(!none && !isMouse);
        cb->setAccessibleName(accessible + QStringLiteral(": ") + label);
        cb->setMinimumHeight(24);
        mods->addWidget(cb, int(boxes.size()) / 2, int(boxes.size()) % 2);
        boxes.push_back(cb);
    }
    mods->setColumnStretch(2, 1);
    v->addLayout(mods);
    auto *kc = new KeyTreePicker(none ? QString() : QString::fromStdString(key), w, spn::ui::buildKeyTree(opt));
    kc->setObjectName(QStringLiteral("sidePickerKey"));
    kc->setAccessibleName(accessible);
    kc->setEnabled(!none);
    kc->setMinimumHeight(24);
    kc->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long key name must not widen the inspector
    kc->setMinimumWidth(120);
    v->addWidget(kc);
    if (picker) *picker = kc;
    for (int i = 1; i < int(boxes.size()); ++i) QWidget::setTabOrder(boxes[size_t(i) - 1], boxes[size_t(i)]);
    QWidget::setTabOrder(boxes.back(), kc);
    auto update = [side, kc, boxes, changed] {
        const std::string chosen = kc->value().toStdString();
        if (chosen.empty()) return;
        if (spn::ui::ChannelForm::isMouseWord(chosen)) {
            *side = chosen;  // a click takes no modifier
        } else {
            std::vector<std::string> on;
            for (QCheckBox *cb : boxes)
                if (cb->isChecked()) on.push_back(cb->property("chordMod").toString().toStdString());
            *side = joinChord(on, chosen);
        }
        changed();
    };
    for (QCheckBox *cb : boxes) QObject::connect(cb, &QCheckBox::toggled, owner, update);
    QObject::connect(kc, &KeyTreePicker::valueChosen, owner, [update](const QString &) { update(); });
    return w;
}
