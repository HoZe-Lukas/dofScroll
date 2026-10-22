// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Press a key" button (issue #174): click it, press the key or key combination, and it is taken over as a chord
// (key_capture_model.hpp). Esc on its own cancels, a click elsewhere or leaving the button cancels too (the key
// list has Esc for the rare case that Esc itself is wanted). Qt reports the keys of the keyboard layout in use;
// they are mapped to the key names of keys.hpp. On macOS Qt swaps Ctrl and Cmd, which is swapped back so that
// "ctrl" is Ctrl and "super" is Cmd, as everywhere else in dofscroll.
#pragma once
#include <QKeyEvent>
#include <QPushButton>
#include <QString>
#include <functional>
#include <string>

#include "ui/model/key_capture_model.hpp"
#include "ui_i18n.hpp"

// The key name of keys.hpp for a key event, "" when dofscroll cannot send the key.
inline std::string captureKeyName(const QKeyEvent *e) {
    const int k = e->key();
    const bool keypad = e->modifiers() & Qt::KeypadModifier;
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return std::string(1, char('a' + (k - Qt::Key_A)));
    if (k >= Qt::Key_0 && k <= Qt::Key_9 && !keypad) return std::string(1, char('0' + (k - Qt::Key_0)));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) return "f" + std::to_string(k - Qt::Key_F1 + 1);
    switch (k) {
        case Qt::Key_Up: return "up";
        case Qt::Key_Down: return "down";
        case Qt::Key_Left: return "left";
        case Qt::Key_Right: return "right";
        case Qt::Key_PageUp: return "pageup";
        case Qt::Key_PageDown: return "pagedown";
        case Qt::Key_Home: return "home";
        case Qt::Key_End: return "end";
        case Qt::Key_Tab:
        case Qt::Key_Backtab: return "tab";
        case Qt::Key_Space: return "space";
        case Qt::Key_Return:
        case Qt::Key_Enter: return "enter";
        case Qt::Key_Escape: return "esc";
        case Qt::Key_Backspace: return "backspace";
        case Qt::Key_Delete: return "delete";
        case Qt::Key_Insert: return "insert";
        case Qt::Key_Minus: return keypad ? "kp_minus" : "minus";
        case Qt::Key_Plus: return keypad ? "kp_plus" : "plus";
        case Qt::Key_Control: return "ctrl";
        case Qt::Key_Shift: return "shift";
        case Qt::Key_Alt: return "alt";
        case Qt::Key_Meta: return "super";
        default: return std::string();
    }
}

inline spn::ui::HeldMods captureMods(const QKeyEvent *e) {
    const Qt::KeyboardModifiers m = e->modifiers();
    spn::ui::HeldMods h;
    h.shift = m & Qt::ShiftModifier;
    h.alt = m & Qt::AltModifier;
#ifdef Q_OS_MACOS
    h.super = m & Qt::ControlModifier;  // Qt: Cmd
    h.ctrl = m & Qt::MetaModifier;      // Qt: Ctrl
#else
    h.ctrl = m & Qt::ControlModifier;
    h.super = m & Qt::MetaModifier;
#endif
    return h;
}

class KeyCaptureButton : public QPushButton {
    Q_OBJECT  // Qt 6.9 insists on it for connections of this class; the header is in the sources of the target for moc
public:
    // `accessible` names the direction ("Negative"); `taken` gets the chord, `refused` the words when the key cannot be sent.
    KeyCaptureButton(const QString &accessible, std::function<void(const std::string &)> taken,
                     std::function<void(const QString &)> refused, QWidget *parent = nullptr)
        : QPushButton(parent), taken_(std::move(taken)), refused_(std::move(refused)), name_(accessible) {
        setAccessibleName(accessible);
        setToolTip(T("Click, then press the key or key combination. Esc cancels."));
        setCheckable(false);
        idle();
        connect(this, &QPushButton::clicked, this, [this] {
            capturing_ = true;
            setText(T("Press the key or combination..."));
            grabKeyboard();
        });
    }
    bool capturing() const { return capturing_; }
    // The keys as the toolkit would deliver them (tests): same path as a real key press.
    void press(QKeyEvent *e) { keyPressEvent(e); }

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (!capturing_) {
            QPushButton::keyPressEvent(e);
            return;
        }
        e->accept();
        const spn::ui::HeldMods mods = captureMods(e);
        if (e->key() == Qt::Key_Escape && !mods.ctrl && !mods.shift && !mods.alt && !mods.super) {
            idle();  // cancelled
            return;
        }
        static const spn::ui::IdentityStrings words;  // the model's English source text; translated here
        const spn::ui::CaptureResult r = spn::ui::captureChord(captureKeyName(e), mods, words);
        if (r.kind == spn::ui::CaptureResult::Waiting) return;  // a modifier alone: wait for its key
        if (r.kind == spn::ui::CaptureResult::Unsupported) {
            idle();
            if (refused_) refused_(T("dofscroll cannot send this key. Choose a key from the list instead."));
            return;
        }
        idle();
        if (taken_) taken_(r.chord);
    }
    void focusOutEvent(QFocusEvent *e) override {
        QPushButton::focusOutEvent(e);
        if (capturing_) idle();
    }

private:
    void idle() {
        if (capturing_) releaseKeyboard();
        capturing_ = false;
        setText(T("Press a key..."));
    }
    std::function<void(const std::string &)> taken_;
    std::function<void(const QString &)> refused_;
    QString name_;
    bool capturing_ = false;
};
