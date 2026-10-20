// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/extensions_page.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "seed.hpp"
#include "ui_i18n.hpp"

using namespace spn::ui;

namespace {
enum { kRoleKey = Qt::UserRole };

QString keyOf(const ExtensionEntry &e) {
    return QString::fromLatin1(e.kind == ExtensionEntry::Kind::JoystickMode ? "mode:" : "button:") + qs8(e.id);
}
}  // namespace

QWidget *createExtensionsPage(const UiContext &ctx) { return new ExtensionsPage(ctx); }

ExtensionsPage::ExtensionsPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent),
      ctx_(ctx),
      ownModes_(ctx.modes ? nullptr : new ModeCatalog(ctx.modesDir)),
      ownButtons_(ctx.buttonModes ? nullptr : new ButtonModeCatalog(ctx.buttonModesDir)),
      modes_(ctx.modes ? *ctx.modes : *ownModes_),
      buttons_(ctx.buttonModes ? *ctx.buttonModes : *ownButtons_) {
    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("<b>") + T("Extensions") + QStringLiteral("</b>"), this));
    auto *intro = new QLabel(T("dofscroll is extended with plain files: joystick modes and button modes. Nothing "
                               "executable. Here you see where each one comes from, add the examples that ship with "
                               "dofscroll and remove your own files. Edit them on the Modes and Button modes pages."),
                             this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::PlainText);
    root->addWidget(intro);

    auto *row = new QHBoxLayout;
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({T("Name"), T("Type"), T("Origin")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);  // never cut a name or a type
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setRootIsDecorated(false);
    tree_->setAccessibleName(T("Extensions"));
    row->addWidget(tree_, 1);
    auto *buttons = new QVBoxLayout;
    auto mk = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    addBtn_ = mk(T("Add example"), T("Copy the selected example into your folder"));
    removeBtn_ = mk(T("Remove"), T("Delete the selected file of yours"));
    modesFolderBtn_ = mk(T("Open modes folder"), T("Show the folder with the mode files"));
    buttonsFolderBtn_ = mk(T("Open button modes folder"), T("Show the folder with the button mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);
    detail_ = new QLabel(this);
    detail_->setWordWrap(true);
    detail_->setTextFormat(Qt::PlainText);
    detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail_->setAccessibleName(T("Details of the selected extension"));
    root->addWidget(detail_);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(addBtn_, &QPushButton::clicked, this, [this] { installSelected(); });
    connect(removeBtn_, &QPushButton::clicked, this, [this] {
        const ExtensionEntry *e = current();
        if (!e) return;
        QString text = T("Delete \"%1\"?").arg(qs8(e->path));
        if (e->replacesBuiltIn) text += QStringLiteral("\n\n") + T("The built-in mode comes back.");
        text += QStringLiteral("\n\n") + T("This cannot be undone.");
        QMessageBox box(QMessageBox::Question, T("Remove"), text, QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(T("Remove"), QMessageBox::DestructiveRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() == yes) removeSelected();
    });
    auto openFolder = [this](const std::string &dir) {
        std::string err;
        if (!spn::fs::mkdirsPrivate(dir, &err)) return showError(T("Could not create the folder."), err);
        if (ctx_.runner && !ctx_.runner->openPath(dir, &err)) showError(T("Could not open the file."), err);
    };
    connect(modesFolderBtn_, &QPushButton::clicked, this, [this, openFolder] { openFolder(ctx_.modesDir); });
    connect(buttonsFolderBtn_, &QPushButton::clicked, this, [this, openFolder] { openFolder(ctx_.buttonModesDir); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ExtensionsPage::refreshIfChanged);
    populate(QString());
}

int ExtensionsPage::extensionCount() const { return tree_->topLevelItemCount(); }

void ExtensionsPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refreshIfChanged();
    timer_->start();
}

void ExtensionsPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
}

void ExtensionsPage::refreshIfChanged() {
    const ExtensionEntry *cur = current();
    const QString keep = cur ? keyOf(*cur) : QString();
    const bool a = modes_.refreshIfChanged(), b = buttons_.refreshIfChanged();
    if (a || b) populate(keep);
}

const ExtensionEntry *ExtensionsPage::current() const {
    const QTreeWidgetItem *it = tree_->currentItem();
    if (!it) return nullptr;
    const QString key = it->data(0, kRoleKey).toString();
    for (const ExtensionEntry &e : entries_)
        if (keyOf(e) == key) return &e;
    return nullptr;
}

void ExtensionsPage::populate(const QString &keepKey) {
    QtStrings str;
    entries_ = buildExtensions(modes_, buttons_, spn::examplesDir());
    const QSignalBlocker block(tree_);
    tree_->clear();
    QTreeWidgetItem *select = nullptr;
    for (const ExtensionEntry &e : entries_) {
        auto *it = new QTreeWidgetItem(tree_);
        it->setText(0, qs8(e.label));
        it->setText(1, qs8(extensionKindText(e.kind, str)));
        QString origin = qs8(extensionOriginText(e, str));
        if (e.warnings) origin += QStringLiteral(" - ") + T("warnings: %1").arg(int(e.warnings));
        it->setText(2, origin);
        it->setData(0, kRoleKey, keyOf(e));
        it->setToolTip(0, qs8(e.description));
        if (!select || keyOf(e) == keepKey) select = it;
    }
    if (select) tree_->setCurrentItem(select);
    for (int c = 0; c < 3; ++c) tree_->resizeColumnToContents(c);
    updateButtons();
}

void ExtensionsPage::updateButtons() {
    const ExtensionEntry *e = current();
    addBtn_->setEnabled(e && e->installable());
    removeBtn_->setEnabled(e && e->removable());
    QString t;
    if (e) {
        t = qs8(e->description);
        if (!e->path.empty()) t += (t.isEmpty() ? QString() : QStringLiteral("\n")) + T("File: %1").arg(qs8(e->path));
    }
    detail_->setText(t);
}

bool ExtensionsPage::selectExtension(const QString &kindAndId) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        if (tree_->topLevelItem(i)->data(0, kRoleKey).toString() == kindAndId) {
            tree_->setCurrentItem(tree_->topLevelItem(i));
            return true;
        }
    return false;
}

bool ExtensionsPage::installSelected() {
    const ExtensionEntry *e = current();
    if (!e || !e->installable()) return false;
    const QString key = keyOf(*e);
    const ExtensionResult r = installExtension(modes_, buttons_, *e);
    if (!r.ok) {
        showError(T("Could not add the example."), r.error);
        return false;
    }
    populate(key);
    return true;
}

bool ExtensionsPage::removeSelected() {
    const ExtensionEntry *e = current();
    if (!e || !e->removable()) return false;
    const QString key = keyOf(*e);
    const ExtensionResult r = removeExtension(modes_, buttons_, *e);
    if (!r.ok) {
        showError(T("Could not remove the file."), r.error);
        return false;
    }
    populate(key);
    return true;
}

void ExtensionsPage::showError(const QString &what, const std::string &detail) {
    QMessageBox::warning(this, T("Extensions"), what + QStringLiteral("\n\n") + qs8(detail));
}
