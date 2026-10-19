// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Modes" page: a thin view on spn::ui::ModeCatalog. List of all modes (built-in, file, built-in
// overridden by a file), detail pane with description and loader warnings, and the actions New,
// Duplicate, Import, Edit, Delete, Open folder. The folder is watched by polling the catalog's
// stamp once a second while the page is visible (no file-system watcher dependency).
#pragma once
#include <QWidget>
#include <memory>
#include <string>

#include "ui/model/mode_catalog.hpp"
#include "ui_context.hpp"

class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class ModesPage : public QWidget {
    Q_OBJECT
public:
    explicit ModesPage(const UiContext &ctx, QWidget *parent = nullptr);
    int modeCount() const;                     // entries in the list
    const spn::ui::ModeCatalog &catalog() const { return catalog_; }

public slots:
    void refreshIfChanged();  // timer; also callable directly

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void populate(const QString &selectId);
    void updateDetail();
    const spn::ui::ModeCatalog::ModeEntry *current() const;
    void onNew();
    void onDuplicate();
    void onImport();
    void onEdit();
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
    QPushButton *newBtn_ = nullptr, *dupBtn_ = nullptr, *importBtn_ = nullptr, *editBtn_ = nullptr,
                *delBtn_ = nullptr, *folderBtn_ = nullptr;
    QTimer *timer_ = nullptr;
};

QWidget *makeModesPage(const UiContext &ctx);
