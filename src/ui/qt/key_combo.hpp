// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key chooser shared by the pages: grouped list, group names are disabled header rows.
#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <QStandardItemModel>

#include "key_tree_picker.hpp"
#include "ui/model/key_list.hpp"
#include "ui_i18n.hpp"

// Older grouped drop-down; the pages use makeKeyTreePicker (key_tree_picker.hpp).
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

// Chord editor (modifier check boxes plus the key picker) shared by the keys:/step: channel editor and the
// key: button action. The chord text is "ctrl+shift+pageup" (modifiers in the fixed order ctrl, shift, alt, super).
// "ctrl+shift+pageup" -> {ctrl, shift} + "pageup"; a lone modifier is the key itself.
inline void splitChord(const std::string &chord, std::vector<std::string> *mods, std::string *key) {
    mods->clear();
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t p; (p = chord.find('+', start)) != std::string::npos; start = p + 1) parts.push_back(chord.substr(start, p - start));
    parts.push_back(chord.substr(start));
    *key = parts.back();
    parts.pop_back();
    *mods = parts;
}

inline constexpr const char *kModNames[4] = {"ctrl", "shift", "alt", "super"};

inline std::string joinChord(const std::vector<std::string> &mods, const std::string &key) {
    std::string out;
    for (const char *m : kModNames)
        if (std::find(mods.begin(), mods.end(), m) != mods.end()) out += std::string(m) + "+";
    return out + key;
}

// The label of a modifier for the people: translated, capitalised, the super key by the platform's name.
inline QString modifierLabel(const std::string &mod) {
    if (mod == "ctrl") return TK("Ctrl");
    if (mod == "shift") return TK("Shift");
    if (mod == "alt") return TK("Alt");
#if defined(Q_OS_MACOS)
    return T("Cmd");
#elif defined(Q_OS_WIN)
    return T("Windows key");
#else
    return T("Super key");
#endif
}

// Modifier check boxes and the key picker of one side of a keys:/step: action.
inline QWidget *chordPicker(QWidget *parent, QObject *owner, std::string *chord, const QString &accessible,
                     const std::function<void()> &changed) {
    auto *w = new QWidget(parent);
    // The modifier boxes in one row, the key picker and its hint under them: one row of all of it needs 550 px in a wide font and
    // does not fit the narrow inspector of the Motions and buttons tab (found by the layout audit).
    auto *col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    auto *h = new QGridLayout;  // two boxes per line: four boxes in a row need 500 px in a wide font and a check box cannot wrap
    h->setContentsMargins(0, 0, 0, 0);
    col->addLayout(h);
    int modIndex = 0;
    std::vector<std::string> mods;
    std::string key;
    splitChord(*chord, &mods, &key);
    for (const char *m : kModNames) {
        const QString label = modifierLabel(m);
        auto *cb = new QCheckBox(label, w);
        cb->setProperty("look", QStringLiteral("box"));
        cb->setProperty("chordMod", QString::fromLatin1(m));  // the stored name; the text is for the people
        cb->setChecked(std::find(mods.begin(), mods.end(), m) != mods.end());
        cb->setAccessibleName(accessible + QStringLiteral(" ") + label);
        h->addWidget(cb, modIndex / 2, modIndex % 2);
        ++modIndex;
    }
    h->setColumnStretch(2, 1);
    auto *kc = makeKeyTreePicker(QString::fromStdString(key), w);
    kc->setAccessibleName(accessible);
    auto *keyRow = new QHBoxLayout;
    col->addLayout(keyRow);
    kc->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long key name must not widen the row
    kc->setMinimumWidth(120);
    keyRow->addWidget(kc, 1);
    // a modifier is ticked and no key chosen yet: nothing is written until there is one, and the line says so
    auto *hint = new QLabel(T("Choose a key"), w);
    hint->setObjectName(QStringLiteral("chordHint"));
    hint->setTextFormat(Qt::PlainText);
    hint->setVisible(false);
    keyRow->addWidget(hint);
    auto showHint = [w, kc, hint] {
        bool any = false;
        for (QCheckBox *cb : w->findChildren<QCheckBox *>()) any = any || cb->isChecked();
        hint->setVisible(any && kc->value().isEmpty());
    };
    showHint();
    auto update = [w, chord, kc, changed, showHint] {
        std::vector<std::string> sel;
        const auto boxes = w->findChildren<QCheckBox *>();
        for (QCheckBox *cb : boxes)
            if (cb->isChecked()) sel.push_back(cb->property("chordMod").toString().toStdString());
        showHint();
        if (kc->value().isEmpty()) return;
        *chord = joinChord(sel, kc->value().toStdString());
        changed();
    };
    for (QCheckBox *cb : w->findChildren<QCheckBox *>()) QObject::connect(cb, &QCheckBox::toggled, owner, update);
    QObject::connect(kc, &KeyTreePicker::valueChosen, owner, [update](const QString &) { update(); });
    return w;
}

