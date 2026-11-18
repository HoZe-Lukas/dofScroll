// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The "Press a key..." button of the two chord pickers (chordPicker in key_combo.hpp, sidePicker in side_picker.hpp), so that every
// place that takes a key or a key combination can be filled from the keyboard as well as from the list (#174), the same everywhere.
// The button takes the chord, ticks the modifier boxes, sets the key in the list and reports the change the way a click in the
// list does; a key dofscroll cannot send is refused in words under the button.
#pragma once
#include <QCheckBox>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "key_capture.hpp"
#include "chord_text.hpp"
#include "key_tree_picker.hpp"
#include "qt_action_runner.hpp"
#include "ui/model/key_tree_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui_i18n.hpp"

// Adds the button and its refusal line to `layout` (the vertical layout of the picker). `boxes` are the modifier boxes, `kc` the
// key list; `apply` runs the picker's own update (it writes the chord and reports the change). Object names for tests: pickerCapture,
// pickerCaptureRefused. The accessible name is "<accessible>: Press a key...".
inline QString pickerCaptureText(const std::string &chord, KeyTreePicker *kc);

inline KeyCaptureButton *addPickerCapture(QWidget *w, QVBoxLayout *layout, const QString &accessible, const std::vector<QCheckBox *> &boxes,
                                          KeyTreePicker *kc, std::string *chord, const std::function<void()> &apply) {
    auto *refused = new QLabel(w);
    refused->setObjectName(QStringLiteral("pickerCaptureRefused"));
    refused->setWordWrap(true);
    refused->setTextFormat(Qt::PlainText);
    refused->hide();
    auto shown = std::make_shared<QPointer<KeyCaptureButton>>();  // the button, for the text that follows the chord
    auto *capture = new KeyCaptureButton(
        accessible + QStringLiteral(": ") + T("Press a key..."),
        [refused, boxes, kc, chord, apply, shown](const std::string &taken) {
            refused->hide();
            std::vector<std::string> sel;
            std::string key;
            splitChord(taken, &sel, &key);
            for (QCheckBox *cb : boxes) {
                cb->blockSignals(true);  // one change for the whole chord, not one per box
                cb->setEnabled(true);
                cb->setChecked(std::find(sel.begin(), sel.end(), cb->property("chordMod").toString().toStdString()) != sel.end());
                cb->blockSignals(false);
            }
            kc->setEnabled(true);
            kc->setValue(QString::fromStdString(key));
            // after the key event is done: the host may rebuild the page, which deletes this button and its picker
            QMetaObject::invokeMethod(
                refused,
                [apply, shown, kc, chord] {
                    apply();
                    if (*shown) (*shown)->setCurrent(pickerCaptureText(*chord, kc));
                },
                Qt::QueuedConnection);
        },
        [refused](const QString &why) {
            refused->setText(mark(spn::ui::StatusKind::Error, why));
            refused->show();
        },
        w);
    *shown = capture;
    capture->setObjectName(QStringLiteral("pickerCapture"));
    capture->setAccessibleDescription(T("Click, then press the key or key combination on your keyboard"));
    capture->setMinimumHeight(24);
    capture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long text must not widen the inspector
    capture->setMinimumWidth(120);
    layout->addWidget(capture);
    layout->addWidget(refused);
    if (!boxes.empty()) QWidget::setTabOrder(kc, capture);
    // the button shows what is set; connected after the picker's own handlers, so the chord is written by then
    auto refresh = [capture, kc, chord] { capture->setCurrent(pickerCaptureText(*chord, kc)); };
    for (QCheckBox *cb : boxes) QObject::connect(cb, &QCheckBox::toggled, capture, refresh);
    QObject::connect(kc, &KeyTreePicker::valueChosen, capture, [refresh](const QString &) { refresh(); });
    refresh();
    return capture;
}

// What the button shows: the chord as the list shows it ("Ctrl+Page up"), "" when there is none.
inline QString pickerCaptureText(const std::string &chord, KeyTreePicker *kc) {
    if (chord.empty() || chord == "none") return QString();
    if (spn::ui::ChannelForm::isMouseWord(chord)) return kc->text();  // "Right click", as the list shows it
    static const QtStrings strings;
    return QString::fromStdString(spn::ui::chordDisplay(chord, strings, [](const std::string &p) { return spn::ui::keyTreeLabel(p); }));
}
