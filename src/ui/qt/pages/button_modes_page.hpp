// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Button modes" page: a thin view on spn::ui::ButtonModeCatalog. List of the button modes (each
// with its origin), an in-page editor (name, description, action) that saves by itself (a drop-down at
// once, typed text after a pause; nothing is written while the form is incomplete, and nothing asks
// about unsaved changes), and New, New from example, Duplicate, Import, Delete, Open folder. The
// folder is watched by polling the catalog stamp once a second while the page is visible.
#pragma once
#include <QWidget>
#include <memory>
#include <string>

#include "autosaver.hpp"
#include "motion_view.hpp"
#include "qt_action_runner.hpp"

#include "ui/model/binding_editor_model.hpp"
#include "ui/model/button_mode_catalog.hpp"
#include "ui_context.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class ButtonModesPage : public QWidget {
    Q_OBJECT
public:
    explicit ButtonModesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int buttonModeCount() const;
    const spn::ui::ButtonModeCatalog &catalog() const { return catalog_; }
    bool pending() const { return saver_ && saver_->pending(); }  // a change waits for its write
    bool flush() { return saver_ ? saver_->flush() : true; }      // writes it now
    // New from example: the ids of the examples that are not installed yet, and copying one (no dialog).
    QStringList exampleIds() const;
    bool installExample(const QString &id);
    QString originOf(const QString &id) const;
    bool exportButtonModeTo(const QString &id, const QString &dest);  // no dialog; false + lastError()
    const QString &lastError() const { return lastError_; }
    QString currentId() const;  // id of the selected list row
    bool selectButtonMode(const QString &id);  // self-test: select a row as a click would
    // For the self-test: set the editor fields as the user would (stageEdit), and write them too (editCurrent).
    bool stageEdit(const QString &name, const QString &description, const QString &action);
    bool editCurrent(const QString &name, const QString &description, const QString &action);

public slots:
    void refreshIfChanged();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void populate(const QString &selectId);
    void loadForm();           // fills the editor from the selected entry
    void refreshActionChip();  // the chip of the chosen action
    void rebuildActionArg();   // argument widget that fits the chosen action kind
    void updateWarnings();
    void markDirty(bool typed);
    void onCurrentChanged();
    spn::ui::Autosave::Outcome writeForm();  // the form saved under shownId_; not written while it is incomplete
    void showProblem(const QString &text);
    void selectRow(const QString &id);  // without signals
    std::string composedAction() const;
    const spn::ui::ButtonModeCatalog::Entry *current() const;
    void onNew();
    void onDuplicate();
    void onImport();
    void onNewFromExample();
    void onExport();
    void onDelete();
    void onEditableCopy();  // built-in button mode: file with the same id that replaces it
    void onOpenFolder();
    void onOpenInEditor();
    bool openInEditor(const std::string &path);
    void showError(const QString &what, const std::string &detail);
    QString askName(const QString &title, const QString &label, const QString &initial, bool *ok);

    UiContext ctx_;
    std::unique_ptr<spn::ui::ButtonModeCatalog> ownCatalog_;
    spn::ui::ButtonModeCatalog &catalog_;
    QListWidget *list_ = nullptr;
    QLineEdit *name_ = nullptr, *description_ = nullptr;
    QComboBox *kind_ = nullptr;
    QWidget *argHost_ = nullptr;
    QWidget *argWidget_ = nullptr;
    QLabel *fileLabel_ = nullptr;
    motionview::ButtonChips *actionChip_ = nullptr;
    QPlainTextEdit *warnings_ = nullptr;
    QLabel *problem_ = nullptr;
    QPushButton *newBtn_ = nullptr, *exampleBtn_ = nullptr, *exportBtn_ = nullptr, *copyBtn_ = nullptr, *dupBtn_ = nullptr, *importBtn_ = nullptr, *delBtn_ = nullptr,
                *folderBtn_ = nullptr, *textEditorBtn_ = nullptr;
    std::unique_ptr<Autosaver> saver_;
    QtStrings ownStrings_;
    QString lastError_;
    QTimer *timer_ = nullptr;
    spn::ui::Binding binding_;  // the action as the editor shows it
    QString shownId_;  // the button mode the form shows (the list may be moved back to it)
    bool dirty_ = false;  // the form holds a change that is not written yet
    bool loading_ = false;
    bool keepForm_ = false;  // populate() after a write: do not reload the form
};

QWidget *createButtonModesPage(const UiContext &ctx);
