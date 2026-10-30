// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// One key field of the "New function..." dialog and of the macro editor (decision 5 of docs/plan-motion-functions.md): the
// modifier check boxes (ctrl, shift, alt, super) plus either the key drop-down (the key tree picker) or the "Press a key..."
// capture, never free text. Optionally the mouse buttons in the drop-down (a hold or a click) and a "No key" box that makes the
// field "none". The value is the chord text of the file ("ctrl+pageup", "none", "rightclick").
#pragma once
#include <QWidget>
#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class KeyTreePicker;
class KeyCaptureButton;

class ChordField : public QWidget {
    Q_OBJECT
public:
    struct Options {
        bool mouse = false;      // the mouse buttons are among the keys of the drop-down (a click or a held button)
        bool allowNone = false;  // the "No key" / "Nothing" box
        QString noneText;        // text of that box ("No key", "Nothing")
    };
    // `accessible` names the field for a screen reader ("Key for the negative direction"); the purpose of every control is its
    // description. The visible label of the field takes keyPicker() as its buddy.
    ChordField(const QString &accessible, const Options &options, QWidget *parent = nullptr);

    std::string value() const;      // "" while a modifier is ticked and no key is chosen yet
    void setValue(const std::string &chord);  // does not emit changed()
    bool isNone() const;
    QWidget *keyPicker() const;     // the drop-down, the buddy of the label
    QWidget *firstControl() const;  // the first control in the Tab order (the first modifier box)
    KeyCaptureButton *captureButton() const { return capture_; }
    // For the self-test: ticks a modifier / chooses a key as the user would (emits changed()).
    void chooseKey(const QString &key);
    void setModifier(const QString &name, bool on);

signals:
    void changed();  // the user changed the value and it is complete

private:
    void update();
    void refreshEnabled();
    std::vector<QCheckBox *> mods_;
    KeyTreePicker *picker_ = nullptr;
    KeyCaptureButton *capture_ = nullptr;
    QCheckBox *none_ = nullptr;
    QLabel *hint_ = nullptr, *refused_ = nullptr;
    bool mouse_ = false, quiet_ = false;
};
