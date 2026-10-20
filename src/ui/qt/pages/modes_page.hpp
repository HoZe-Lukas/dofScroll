// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Joystick modes" page: a thin view on spn::ui::ModeCatalog. List of all joystick modes (built-in, file,
// built-in overridden by a file) with the origin of each, detail pane with description and loader
// warnings, and the actions New, New from example, Duplicate, Import, Edit, Delete, Open folder. The
// editor saves by itself (see mode_editor_widget.hpp); nothing asks about unsaved changes. The folder is watched by polling the catalog's
// stamp once a second while the page is visible (no file-system watcher dependency).
#pragma once
#include <QWidget>
#include <QStringList>
#include <memory>
#include <string>

#include "ui/model/mode_catalog.hpp"
#include "ui_context.hpp"

class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class ModeEditorWidget;
class QTimer;
class QDragEnterEvent;
class QDropEvent;

class ModesPage : public QWidget {
    Q_OBJECT
public:
    explicit ModesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int modeCount() const;                     // entries in the list
    bool selectMode(const QString &id);        // selects a mode in the list (self-test, other pages)
    bool openEditor();                         // the in-page editor for the selected mode (a file mode)
    ModeEditorWidget *editorWidget() const { return editor_; }
    // Mode library: the profiles that use the selected mode, as text lines for the details pane.
    QStringList usedByLines(const QString &id) const;
    // Import of a dropped or chosen file; with `interactive` it asks before keeping the file and
    // shows errors, without it (self-test) it only reports through lastError(). True if imported.
    bool importPath(const QString &file, bool interactive = true);
    // No dialogs; failures are reported through lastError(). Rename does not rewrite profiles.
    bool exportModeTo(const QString &id, const QString &dest);
    bool renameMode(const QString &id, const QString &newName);
    const QString &lastError() const { return lastError_; }
    const spn::ui::ModeCatalog &catalog() const { return catalog_; }
    QString currentId() const;                 // id of the selected list row
    // True while the in-page editor is shown and a change waits for its write (a pause or a flush).
    bool pendingEdits() const;
    // Writes the pending change of the editor now (page switch, another mode, quit).
    void flushEditor();
    // New from example: the ids of the examples that are not installed yet, and copying one (no dialog).
    QStringList exampleIds() const;
    bool installExample(const QString &id);
    QString originOf(const QString &id) const;  // "Built in", "Your file", ... of a listed joystick mode

public slots:
    void refreshIfChanged();  // timer; also callable directly

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void populate(const QString &selectId);
    void updateDetail();
    void onCurrentChanged();
    void selectRow(const QString &id);  // without signals
    const spn::ui::ModeCatalog::ModeEntry *current() const;
    void onNew();
    void onNewFromExample();
    void onDuplicate();
    void onImport();
    void onEdit();
    void onExport();
    void onRename();
    void onDelete();
    void onOpenFolder();
    bool openInEditor(const std::string &path);
    void showError(const QString &what, const std::string &detail);
    QString askName(const QString &title, const QString &label, const QString &initial, bool *ok);

    UiContext ctx_;
    std::unique_ptr<spn::ui::ModeCatalog> ownCatalog_;  // only when the context has no shared one
    spn::ui::ModeCatalog &catalog_;
    QListWidget *list_ = nullptr;
    QPlainTextEdit *detail_ = nullptr;
    QStackedWidget *right_ = nullptr;  // details | editor
    ModeEditorWidget *editor_ = nullptr;
    QPushButton *newBtn_ = nullptr, *exampleBtn_ = nullptr, *dupBtn_ = nullptr, *importBtn_ = nullptr, *editBtn_ = nullptr,
                *delBtn_ = nullptr, *folderBtn_ = nullptr, *exportBtn_ = nullptr, *renameBtn_ = nullptr;
    QTimer *timer_ = nullptr;
    QString shownId_;  // the mode whose details or editor are shown (the list may be moved back to it)
    QString lastError_;
};

QWidget *makeModesPage(const UiContext &ctx);
