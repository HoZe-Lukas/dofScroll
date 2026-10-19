// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Button modes" page: a thin view on spn::ui::ButtonModeCatalog. List of the button modes, an
// in-page editor (name, description, action) with Save and Revert, and New, Duplicate, Import,
// Delete, Open folder. The folder is watched by polling the catalog stamp once a second while the
// page is visible.
#pragma once
#include <QWidget>
#include <memory>
#include <string>

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
    bool dirty() const { return dirty_; }
    // For the self-test: set the editor fields as the user would and press Save.
    bool editCurrent(const QString &name, const QString &description, const QString &action);

public slots:
    void refreshIfChanged();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void populate(const QString &selectId);
    void loadForm();           // fills the editor from the selected entry
    void rebuildActionArg();   // argument widget that fits the chosen action kind
    void markDirty();
    std::string composedAction() const;
    const spn::ui::ButtonModeCatalog::Entry *current() const;
    void onNew();
    void onDuplicate();
    void onImport();
    void onSave();
    void onDelete();
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
    QPlainTextEdit *warnings_ = nullptr;
    QPushButton *newBtn_ = nullptr, *dupBtn_ = nullptr, *importBtn_ = nullptr, *delBtn_ = nullptr,
                *folderBtn_ = nullptr, *saveBtn_ = nullptr, *revertBtn_ = nullptr, *textEditorBtn_ = nullptr;
    QTimer *timer_ = nullptr;
    spn::ui::Binding binding_;  // the action as the editor shows it
    bool dirty_ = false;
    bool loading_ = false;
};

QWidget *createButtonModesPage(const UiContext &ctx);
