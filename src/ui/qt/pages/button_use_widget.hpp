// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Use it on a button" on the Button modes page (issue #143, phase 4): the buttons of the device as in the joystick
// mode editor, showing what the Global profile binds to each of them. Choose a button, then "Use <button mode> on
// button n" binds the selected button mode to it in the Global profile, or "Unbind" removes the binding. The
// state is in words: which buttons use the button mode now ("Used on buttons: 1, 4 (Global profile)"). Written by
// the autosave of the settings like every other edit; there is no Save button. Motions are not part of this
// view: a button mode only ever belongs to buttons.
#pragma once
#include <QString>
#include <QWidget>

#include "ui_context.hpp"

class MotionPicture;
class QLabel;
class QPushButton;

class ButtonUseWidget : public QWidget {
    Q_OBJECT
public:
    explicit ButtonUseWidget(const UiContext &ctx, QWidget *parent = nullptr);
    // The button mode to bind: its id and the name shown ("" id: nothing selected, the buttons are shown only).
    void setButtonMode(const QString &id, const QString &label);
    void refresh();
    MotionPicture *picture() const { return picture_; }
    QString status() const;
    QString selectedId() const;
    bool select(const QString &id);
    QPushButton *useButton() const { return use_; }
    QPushButton *unbindButton() const { return unbind_; }

private:
    void updateButtons();

    UiContext ctx_;
    QString id_, label_;
    MotionPicture *picture_ = nullptr;
    QLabel *status_ = nullptr, *selected_ = nullptr;
    QPushButton *use_ = nullptr, *unbind_ = nullptr;
};
