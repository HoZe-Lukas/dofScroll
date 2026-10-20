// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// In-page editor for a joystick mode file (Modes page): name, description, the three channels
// (including chords and step:), the buttons (the shared button row) and the settings overrides.
// A thin view on spn::ui::ModeEditor, which owns the logic (comment-preserving save, validation,
// loader check, change detection).
#pragma once
#include <QWidget>
#include <string>

#include "ui/model/mode_editor_model.hpp"
#include "qt_action_runner.hpp"
#include "ui_context.hpp"

class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;

class ModeEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModeEditorWidget(const UiContext &ctx, QWidget *parent = nullptr);
    // Opens the file of a loaded mode; false (and a message box) if it cannot be read.
    bool open(const std::string &path, const spn::ModeDef &def);
    spn::ui::ModeEditor &editor() { return editor_; }
    const spn::ui::ModeEditor &editor() const { return editor_; }
    // Saves like the Save button (without asking about a changed file). false: refused.
    bool save(bool overwriteChanged = false);

signals:
    void closeRequested();
    void saved(const QString &id);

private:
    void rebuild();  // all widgets from the form
    int addChannelRow(QGridLayout *g, int row, const QString &label, spn::ui::ChannelForm *form);  // returns the rows used
    void showProblems();
    void onSave();
    void onRevert();

    UiContext ctx_;
    QtStrings ownStrings_;  // used when the context has no shared provider (standalone page, tests)
    spn::ui::ModeEditor editor_;
    std::string dir_;
    QScrollArea *scroll_ = nullptr;
    QLabel *title_ = nullptr, *problems_ = nullptr;
    QPlainTextEdit *warnings_ = nullptr;
    QPushButton *saveBtn_ = nullptr, *revertBtn_ = nullptr, *closeBtn_ = nullptr, *textBtn_ = nullptr;
};
