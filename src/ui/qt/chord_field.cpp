// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "chord_field.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>

#include "key_capture.hpp"
#include "key_combo.hpp"
#include "key_tree_picker.hpp"
#include "ui/model/key_tree_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui_i18n.hpp"

ChordField::ChordField(const QString &accessible, const Options &options, QWidget *parent) : QWidget(parent), mouse_(options.mouse) {
    setObjectName(QStringLiteral("chordField"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    // modifiers: one below the other; boxes side by side are wider than a dialog allows with a wide font ("Windows key")
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    int i = 0;
    for (const char *m : kModNames) {
        const QString label = modifierLabel(m);
        auto *cb = new QCheckBox(label, this);
        cb->setObjectName(QStringLiteral("chordMod_") + QString::fromLatin1(m));
        cb->setProperty("chordMod", QString::fromLatin1(m));  // the stored name; the text is for the people
        cb->setProperty("look", QStringLiteral("box"));
        cb->setAccessibleName(accessible + QStringLiteral(": ") + label);
        cb->setAccessibleDescription(T("Hold this modifier together with the key"));
        cb->setMinimumHeight(24);
        grid->addWidget(cb, i, 0);
        mods_.push_back(cb);
        ++i;
    }
    v->addLayout(grid);
    spn::ui::KeyTreeOptions opt;
    opt.mouse = options.mouse;
    picker_ = new KeyTreePicker(QString(), this, spn::ui::buildKeyTree(opt));
    picker_->setObjectName(QStringLiteral("chordKey"));
    picker_->setAccessibleName(accessible);
    picker_->setAccessibleDescription(T("Choose the key from the list"));
    picker_->setMinimumHeight(24);
    picker_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long key name must not widen the dialog
    picker_->setMinimumWidth(120);
    v->addWidget(picker_);
    capture_ = new KeyCaptureButton(
        accessible + QStringLiteral(": ") + T("Press a key..."),
        [this](const std::string &chord) {
            refused_->hide();
            setValue(chord);
            emit changed();
        },
        [this](const QString &why) {
            refused_->setText(mark(spn::ui::StatusKind::Error, why));
            refused_->show();
        },
        this);
    capture_->setObjectName(QStringLiteral("chordCapture"));
    capture_->setAccessibleDescription(T("Click, then press the key or key combination on your keyboard"));
    capture_->setMinimumHeight(24);
    capture_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    capture_->setMinimumWidth(120);
    v->addWidget(capture_);
    refused_ = new QLabel(this);
    refused_->setObjectName(QStringLiteral("chordRefused"));
    refused_->setWordWrap(true);
    refused_->setTextFormat(Qt::PlainText);
    refused_->hide();
    v->addWidget(refused_);
    hint_ = new QLabel(T("Choose a key"), this);
    hint_->setObjectName(QStringLiteral("chordHint"));
    hint_->setWordWrap(true);
    hint_->setTextFormat(Qt::PlainText);
    hint_->hide();
    v->addWidget(hint_);
    if (options.allowNone) {
        none_ = new QCheckBox(options.noneText.isEmpty() ? T("No key") : options.noneText, this);
        none_->setObjectName(QStringLiteral("chordNone"));
        none_->setProperty("look", QStringLiteral("box"));
        none_->setAccessibleName(accessible + QStringLiteral(": ") + none_->text());
        none_->setAccessibleDescription(T("Nothing happens in this direction"));
        none_->setMinimumHeight(24);
        v->addWidget(none_);
    }
    // the Tab order is the order on the screen
    QWidget *prev = nullptr;
    for (QCheckBox *cb : mods_) {
        if (prev) QWidget::setTabOrder(prev, cb);
        prev = cb;
    }
    QWidget::setTabOrder(prev, picker_);
    QWidget::setTabOrder(picker_, capture_);
    if (none_) QWidget::setTabOrder(capture_, none_);
    for (QCheckBox *cb : mods_) connect(cb, &QCheckBox::toggled, this, [this] { if (!quiet_) update(); });
    connect(picker_, &KeyTreePicker::valueChosen, this, [this](const QString &key) {
        if (quiet_) return;
        if (spn::ui::ChannelForm::isMouseWord(key.toStdString())) {
            quiet_ = true;
            for (QCheckBox *cb : mods_) cb->setChecked(false);  // a mouse button takes no modifier
            quiet_ = false;
        }
        update();
    });
    if (none_)
        connect(none_, &QCheckBox::toggled, this, [this](bool) {
            refreshEnabled();
            if (!quiet_) update();
        });
    refreshEnabled();
}

bool ChordField::isNone() const { return none_ && none_->isChecked(); }
QWidget *ChordField::keyPicker() const { return picker_; }

std::string ChordField::value() const {
    if (isNone()) return "none";
    const std::string key = picker_->value().toStdString();
    if (key.empty()) return std::string();
    if (spn::ui::ChannelForm::isMouseWord(key)) return key;
    std::vector<std::string> on;
    for (QCheckBox *cb : mods_)
        if (cb->isChecked()) on.push_back(cb->property("chordMod").toString().toStdString());
    return joinChord(on, key);
}

void ChordField::setValue(const std::string &chord) {
    const bool was = quiet_;
    quiet_ = true;
    const bool none = chord == "none";
    if (none_) none_->setChecked(none);
    std::vector<std::string> sel;
    std::string key;
    if (!none) splitChord(chord, &sel, &key);
    const bool isMouse = spn::ui::ChannelForm::isMouseWord(key);
    for (QCheckBox *cb : mods_) cb->setChecked(!isMouse && std::find(sel.begin(), sel.end(), cb->property("chordMod").toString().toStdString()) != sel.end());
    picker_->setValue(none ? QString() : QString::fromStdString(key));
    quiet_ = was;
    refreshEnabled();
}

void ChordField::refreshEnabled() {
    const bool none = isNone();
    const bool isMouse = mouse_ && spn::ui::ChannelForm::isMouseWord(picker_->value().toStdString());
    for (QCheckBox *cb : mods_) cb->setEnabled(!none && !isMouse);
    picker_->setEnabled(!none);
    capture_->setEnabled(!none);
    bool anyMod = false;
    for (QCheckBox *cb : mods_) anyMod = anyMod || cb->isChecked();
    hint_->setVisible(!none && anyMod && picker_->value().isEmpty());
}

void ChordField::update() {
    refreshEnabled();
    if (value().empty()) return;  // a modifier is ticked and no key chosen yet: nothing is reported until there is one
    emit changed();
}

void ChordField::chooseKey(const QString &key) {
    picker_->setValue(key);
    emit picker_->valueChosen(key);
}

void ChordField::setModifier(const QString &name, bool on) {
    for (QCheckBox *cb : mods_)
        if (cb->property("chordMod").toString() == name) cb->setChecked(on);
}
