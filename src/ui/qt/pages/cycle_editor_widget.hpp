// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Cycle through" section of the Button modes page: a thin view on spn::ui::CycleEditorModel. A choice
// between all joystick modes and only the ticked ones, a check list of every loaded joystick mode,
// Move up / Move down for the ticked ones and the resulting order in words. Every edit emits edited();
// the page writes it through its autosave. Keyboard: Tab to the choice, the list (Space ticks) and
// the move buttons; every control has an accessible name.
#pragma once
#include <QGroupBox>
#include <QStringList>
#include <string>
#include <vector>

#include "modes.hpp"
#include "ui/model/cycle_editor_model.hpp"

class QLabel;
class QListWidget;
class QComboBox;
class QToolButton;

class CycleEditorWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit CycleEditorWidget(QWidget *parent = nullptr);
    // Shows the state of a button mode file. `editable` false (a built-in button mode): read-only plus
    // the note what the built-in ones do. Does not emit edited().
    void setState(const spn::ModeSet &modes, const std::vector<std::string> &cycle, bool editable);
    // The CYCLE list to write (empty = no CYCLE key, all joystick modes).
    std::vector<std::string> cycle() const { return model_.cycle(); }
    const spn::ui::CycleEditorModel &model() const { return model_; }
    // The edits as a click or key press does them (the self-test uses the same functions).
    void setOnlySelected(bool only);
    bool setChecked(const QString &id, bool on);
    bool moveUp(const QString &id);
    bool moveDown(const QString &id);
    QStringList shownRows() const;  // the text of the rows, top to bottom

signals:
    void edited();

private:
    void rebuild(const QString &keepId);
    void updateButtons();
    QString currentRowId() const;

    spn::ui::CycleEditorModel model_;
    QComboBox *choice_ = nullptr;  // "All joystick modes" / "Only these joystick modes" (a drop-down: radio buttons are too wide for the narrow editor column)
    QListWidget *list_ = nullptr;
    QToolButton *up_ = nullptr, *down_ = nullptr;
    QLabel *summary_ = nullptr, *hint_ = nullptr, *note_ = nullptr;
    bool editable_ = true;
    bool building_ = false;
};
