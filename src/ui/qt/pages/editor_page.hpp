// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The Editor page (group Modes): the visual joystick mode editor at full width. Choose a joystick mode and click the
// cap's motions or the device buttons to say what they do (issue #143). It is the editor of the Joystick modes page
// (ModeEditorWidget, first tab "Motions and buttons") with a joystick mode chooser instead of the list, so the
// picture has the room it needs. A built-in joystick mode is not a file: the page offers "Make my own copy"
// (a new file with its own name) and edits that. Every change is written by the editor's autosave.
#pragma once
#include <QString>
#include <QWidget>
#include <memory>

#include "ui/model/mode_catalog.hpp"
#include "ui_context.hpp"

class ModeEditorWidget;
class QComboBox;
class QLabel;
class QPushButton;

class EditorPage : public QWidget {
    Q_OBJECT
public:
    explicit EditorPage(const UiContext &ctx, QWidget *parent = nullptr);
    ModeEditorWidget *editorWidget() const { return editor_; }
    QString currentId() const;
    bool selectMode(const QString &id);  // as a click on the chooser would
    bool canEdit() const;                // the chosen joystick mode is a file
    bool makeCopy();                     // "Make my own copy" without the question; the copy is chosen
    QPushButton *copyButton() const { return copy_; }
    QComboBox *chooser() const { return combo_; }
    QString note() const;
    void flush();

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    void populate(const QString &selectId);
    void openCurrent();
    const spn::ui::ModeCatalog::ModeEntry *current() const;

    UiContext ctx_;
    std::unique_ptr<spn::ui::ModeCatalog> ownCatalog_;
    spn::ui::ModeCatalog &catalog_;
    QComboBox *combo_ = nullptr;
    QLabel *note_ = nullptr;
    QPushButton *copy_ = nullptr;
    ModeEditorWidget *editor_ = nullptr;
    QString shown_;
    bool loading_ = false;
};

QWidget *createEditorPage(const UiContext &ctx);
