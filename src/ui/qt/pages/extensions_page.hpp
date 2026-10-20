// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Extensions" page: all modes and button modes in one list with where they come from (built in,
// your file, example not installed), "Add example" and "Remove". A thin view on
// spn::ui::buildExtensions; the editing pages (Modes, Button modes) stay the place to change them.
#pragma once
#include <QWidget>
#include <memory>

#include "ui/model/extensions_model.hpp"
#include "ui_context.hpp"

class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;

class ExtensionsPage : public QWidget {
    Q_OBJECT
public:
    explicit ExtensionsPage(const UiContext &ctx, QWidget *parent = nullptr);
    int extensionCount() const;
    bool selectExtension(const QString &kindAndId);  // "mode:slides" / "button:latch" (self-test)
    bool installSelected();                          // as the button, without a question
    bool removeSelected();                           // as the button, without a question

public slots:
    void refreshIfChanged();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void populate(const QString &keepKey);
    const spn::ui::ExtensionEntry *current() const;
    void updateButtons();
    void showError(const QString &what, const std::string &detail);

    UiContext ctx_;
    std::unique_ptr<spn::ui::ModeCatalog> ownModes_;
    std::unique_ptr<spn::ui::ButtonModeCatalog> ownButtons_;
    spn::ui::ModeCatalog &modes_;
    spn::ui::ButtonModeCatalog &buttons_;
    std::vector<spn::ui::ExtensionEntry> entries_;
    QTreeWidget *tree_ = nullptr;
    QLabel *detail_ = nullptr;
    QPushButton *addBtn_ = nullptr, *removeBtn_ = nullptr, *modesFolderBtn_ = nullptr, *buttonsFolderBtn_ = nullptr;
    QTimer *timer_ = nullptr;
};

QWidget *createExtensionsPage(const UiContext &ctx);
