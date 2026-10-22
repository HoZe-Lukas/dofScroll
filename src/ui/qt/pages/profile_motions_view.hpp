// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The "Motions and buttons" tab of a profile (issue #143, phase 4): the same picture as the joystick mode editor, for the
// profile (Global, device or program) that is selected. The six motions show what the profile's joystick mode
// does with them in words, with where it comes from; the action belongs to the joystick mode, so it is changed
// there. What the profile itself sets is edited here: the tuning of a channel (sensitivity, dead zone, direction,
// curve, smoothing) as overrides of the profile, with the inheritance in words ("Inherited from Global: 5",
// "Set in this profile (inherited: 5)") and a Reset, and the buttons through the shared button row. Everything
// is written by the autosave of the profiles page; there is no Save button.
#pragma once
#include <QLabel>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui_context.hpp"

class MotionPicture;
class QGridLayout;
class QVBoxLayout;

class ProfileMotionsView : public QWidget {
    Q_OBJECT
public:
    using ButtonRowFn = std::function<void(QWidget *parent, QGridLayout *grid, int gridRow, size_t section, int n)>;
    ProfileMotionsView(const UiContext &ctx, ButtonRowFn addButtonRow, QWidget *parent = nullptr);
    // bindingEditor false: the profile's joystick mode brings its own button bindings (the buttons are read-only).
    void setSection(size_t section, bool bindingEditor);
    MotionPicture *picture() const { return picture_; }
    QWidget *inspector() const { return inspector_; }
    QString selectedId() const;
    bool select(const QString &id);
    QString inspectorTitle() const { return title_ ? title_->text() : QString(); }
    QString sentence() const;
    std::function<void()> changed;  // a value was written; the page queues a rebuild of the other tabs

private:
    void rebuild();
    void rebuildInspector();
    void buildMotion(QVBoxLayout *col, const QString &motionId);
    void buildButton(QVBoxLayout *col, int n);

    const UiContext &ctx_;
    ButtonRowFn addButtonRow_;
    size_t section_ = 0;
    bool bindingEditor_ = true;
    MotionPicture *picture_ = nullptr;
    QWidget *inspector_ = nullptr;
    QVBoxLayout *col_ = nullptr;
    QLabel *title_ = nullptr, *sentence_ = nullptr;
    QString selected_;
};
