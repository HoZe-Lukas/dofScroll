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
#include <QList>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/model/motion_edit_model.hpp"
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
    // Widgets shown above the picture (notes about moved old button settings, the Global "Advanced" section); the view owns them.
    // The settings search: a button is drawn (and its row reachable) only when this returns true for its number and
    // its label ("Button 3 <action>"); an empty function shows every button.
    void setButtonFilter(std::function<bool(int number, const QString &label)> filter) { buttonFilter_ = std::move(filter); }
    void setExtras(const QList<QWidget *> &widgets);
    MotionPicture *picture() const { return picture_; }
    QWidget *inspector() const { return inspector_; }
    QString selectedId() const;
    bool select(const QString &id);
    QString inspectorTitle() const { return title_ ? title_->text() : QString(); }
    QString sentence() const;
    // "Take over from ...": asks for a source (another profile, a joystick mode, for a button also a button mode) and copies
    // the effective assignment into this profile (src/ui/model/borrow_model.hpp). False when cancelled or refused (the
    // reason is shown in the inspector).
    bool takeOverMotion(const QString &motionId);
    bool takeOverButton(int n);
    std::function<void()> changed;  // a value was written; the page queues a rebuild of the other tabs

private:
    void rebuild();
    void rebuildInspector();
    void buildMotion(QVBoxLayout *col, const QString &motionId);
    void buildButton(QVBoxLayout *col, int n);

    const UiContext &ctx_;
    ButtonRowFn addButtonRow_;
    std::function<bool(int, const QString &)> buttonFilter_;
    size_t section_ = 0;
    bool bindingEditor_ = true;
    MotionPicture *picture_ = nullptr;
    QWidget *inspector_ = nullptr;
    QVBoxLayout *col_ = nullptr, *extras_ = nullptr;
    QLabel *title_ = nullptr, *sentence_ = nullptr;
    QString selected_, problemText_;
    spn::ui::MotionMemory memory_;  // the sensitivity a channel had before it was cleared (session only)
};
