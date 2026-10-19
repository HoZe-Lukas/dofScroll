// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/modes_page.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui_i18n.hpp"

using spn::ui::ModeCatalog;
using Origin = ModeCatalog::Origin;

namespace {
enum { kRoleId = Qt::UserRole };

QString originText(Origin o) {
    switch (o) {
        case Origin::BuiltIn: return T("built-in");
        case Origin::File: return T("file");
        case Origin::BuiltInOverridden: return T("file, replaces the built-in mode");
    }
    return QString();
}
}  // namespace

QWidget *makeModesPage(const UiContext &ctx) { return new ModesPage(ctx); }

ModesPage::ModesPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx), catalog_(ctx.modesDir) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("<b>") + T("Modes") + QStringLiteral("</b>"), this);
    root->addWidget(title);

    // Permanent hint: a mode file can press any key (also shortcut chords).
    auto *hint = new QLabel(T("Mode files can press any key. Only use mode files from trusted sources."), this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::PlainText);
    root->addWidget(hint);

    auto *row = new QHBoxLayout;
    auto *split = new QSplitter(Qt::Horizontal, this);
    list_ = new QListWidget(split);
    list_->setAccessibleName(T("Modes"));
    list_->setToolTip(T("All modes: built-in modes and the files in the modes folder"));
    detail_ = new QPlainTextEdit(split);
    detail_->setReadOnly(true);  // plain text only: mode files are foreign content
    detail_->setAccessibleName(T("Details of the selected mode"));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    row->addWidget(split, 1);

    auto *buttons = new QVBoxLayout;
    auto mk = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    newBtn_ = mk(T("New mode"), T("Create a mode file from a commented template and open it in your editor"));
    dupBtn_ = mk(T("Duplicate"), T("Copy the selected mode under a new name"));
    importBtn_ = mk(T("Import file..."), T("Add a mode file from somewhere else (you see its warnings first)"));
    editBtn_ = mk(T("Edit"), T("Open the mode file in your default editor"));
    delBtn_ = mk(T("Delete"), T("Delete the mode file (file modes only)"));
    folderBtn_ = mk(T("Open modes folder"), T("Show the folder with the mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);

    // Tab order follows the creation order: list, details, buttons.
    setTabOrder(list_, detail_);
    setTabOrder(detail_, newBtn_);
    setTabOrder(newBtn_, dupBtn_);
    setTabOrder(dupBtn_, importBtn_);
    setTabOrder(importBtn_, editBtn_);
    setTabOrder(editBtn_, delBtn_);
    setTabOrder(delBtn_, folderBtn_);

    connect(list_, &QListWidget::currentItemChanged, this, [this] { updateDetail(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { onEdit(); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { onNew(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this] { onDuplicate(); });
    connect(importBtn_, &QPushButton::clicked, this, [this] { onImport(); });
    connect(editBtn_, &QPushButton::clicked, this, [this] { onEdit(); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { onDelete(); });
    connect(folderBtn_, &QPushButton::clicked, this, [this] { onOpenFolder(); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ModesPage::refreshIfChanged);

    populate(QString());
}

int ModesPage::modeCount() const { return list_->count(); }

void ModesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refreshIfChanged();  // catch up with changes made while hidden
    timer_->start();
}

void ModesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
}

void ModesPage::refreshIfChanged() {
    const ModeCatalog::ModeEntry *cur = current();
    const QString keep = cur ? QString::fromStdString(cur->id) : QString();
    if (catalog_.refreshIfChanged()) populate(keep);
}

const ModeCatalog::ModeEntry *ModesPage::current() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? catalog_.find(it->data(kRoleId).toString().toStdString()) : nullptr;
}

void ModesPage::populate(const QString &selectId) {
    const QSignalBlocker block(list_);
    list_->clear();
    int select = 0, n = 0;
    for (const ModeCatalog::ModeEntry &m : catalog_.modes()) {
        const QString id = QString::fromStdString(m.id), label = QString::fromStdString(m.label);
        auto *it = new QListWidgetItem(label == id ? id : label + QStringLiteral(" (") + id + QStringLiteral(")"));
        it->setData(kRoleId, id);
        if (!m.warnings.empty()) it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        it->setToolTip(originText(m.origin));
        list_->addItem(it);
        if (id == selectId) select = n;
        ++n;
    }
    if (n) list_->setCurrentRow(select);
    updateDetail();
}

void ModesPage::updateDetail() {
    const ModeCatalog::ModeEntry *m = current();
    QString t;
    if (m) {
        t += T("Name: %1").arg(QString::fromStdString(m->label)) + QLatin1Char('\n');
        t += T("Id: %1").arg(QString::fromStdString(m->id)) + QLatin1Char('\n');
        t += T("Origin: %1").arg(originText(m->origin)) + QLatin1Char('\n');
        if (!m->path.empty()) t += T("File: %1").arg(QString::fromStdString(m->path)) + QLatin1Char('\n');
        if (!m->description.empty()) t += QLatin1Char('\n') + QString::fromStdString(m->description) + QLatin1Char('\n');
        if (!m->warnings.empty()) {
            t += QLatin1Char('\n') + T("Warnings:") + QLatin1Char('\n');
            for (const std::string &w : m->warnings) t += QStringLiteral(" - ") + QString::fromStdString(w) + QLatin1Char('\n');
        }
    }
    if (!catalog_.generalWarnings().empty()) {
        t += QLatin1Char('\n') + T("Warnings for the modes folder:") + QLatin1Char('\n');
        for (const std::string &w : catalog_.generalWarnings())
            t += QStringLiteral(" - ") + QString::fromStdString(w) + QLatin1Char('\n');
    }
    detail_->setPlainText(t);
    dupBtn_->setEnabled(m != nullptr);
    editBtn_->setEnabled(m != nullptr);
    delBtn_->setEnabled(m && m->origin != Origin::BuiltIn);
}

void ModesPage::showError(const QString &what, const std::string &detail) {
    QMessageBox::warning(this, T("Modes"), what + QStringLiteral("\n\n") + QString::fromStdString(detail));
}

bool ModesPage::openInEditor(const std::string &path) {
    std::string err;
    if (ctx_.runner && ctx_.runner->openPath(path, &err)) return true;
    showError(T("Could not open the file."), err);
    return false;
}

QString ModesPage::askName(const QString &title, const QString &label, const QString &initial, bool *ok) {
    return QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, ok).trimmed();
}

void ModesPage::onNew() {
    bool ok = false;
    const QString name = askName(T("New mode"), T("Name of the new mode:"), QString(), &ok);
    if (!ok) return;
    const ModeCatalog::Result r = catalog_.createNew(name.toStdString());
    if (!r.ok) return showError(T("Could not create the mode."), r.error);
    populate(QString::fromStdString(r.id));
    openInEditor(r.path);
}

void ModesPage::onDuplicate() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    bool ok = false;
    const QString name = askName(T("Duplicate"), T("Name of the copy:"),
                                 T("%1 (copy)").arg(QString::fromStdString(m->label)), &ok);
    if (!ok) return;
    const ModeCatalog::Result r = catalog_.duplicate(m->id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the mode."), r.error);
    populate(QString::fromStdString(r.id));
}

void ModesPage::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, T("Import mode file"), QDir::homePath(),
                                                      T("Mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    const std::string path = file.toStdString();
    std::string name;  // empty: the id comes from the file name
    ModeCatalog::Result chk = catalog_.inspectImport(path, name);
    if (!chk.ok && chk.error.find("already exists") != std::string::npos) {
        // The id is taken: let the user pick another name.
        bool ok = false;
        const QString n = askName(T("Import mode file"), T("A mode with this id already exists. Name for the imported mode:"),
                                  QFileInfo(file).completeBaseName(), &ok);
        if (!ok) return;
        name = n.toStdString();
        chk = catalog_.inspectImport(path, name);
    }
    if (!chk.ok) return showError(T("This file cannot be imported."), chk.error);

    // Show the warnings and the permanent safety note before anything is kept.
    QString text = T("Import the mode \"%1\"?").arg(QString::fromStdString(chk.id));
    if (!chk.warnings.empty()) {
        text += QStringLiteral("\n\n") + T("Warnings:");
        for (const std::string &w : chk.warnings) text += QStringLiteral("\n - ") + QString::fromStdString(w);
    } else {
        text += QStringLiteral("\n\n") + T("The file has no loader warnings.");
    }
    QMessageBox box(QMessageBox::Warning, T("Import mode file"), text, QMessageBox::NoButton, this);
    box.setInformativeText(QString::fromStdString(ModeCatalog::importSafetyNote(QtStrings())));
    QPushButton *yes = box.addButton(T("Import"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;

    const ModeCatalog::Result r = catalog_.importFile(path, name);
    if (!r.ok) return showError(T("Could not import the mode."), r.error);
    populate(QString::fromStdString(r.id));
}

void ModesPage::onEdit() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    if (!m->path.empty()) {
        openInEditor(m->path);
        return;
    }
    // A built-in mode has no file: offer an editable copy with the same id that replaces it.
    const QString id = QString::fromStdString(m->id);
    const QMessageBox::StandardButton a = QMessageBox::question(
        this, T("Edit"),
        T("The built-in mode \"%1\" is not a file. Create an editable file with the same id? It replaces the "
          "built-in mode until you delete the file. To keep the original, use Duplicate instead.")
            .arg(id),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (a != QMessageBox::Yes) return;
    const ModeCatalog::Result r = catalog_.makeEditableCopy(m->id);
    if (!r.ok) return showError(T("Could not create the file."), r.error);
    populate(id);
    openInEditor(r.path);
}

void ModesPage::onDelete() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->origin == Origin::BuiltIn) return;
    const QString id = QString::fromStdString(m->id);
    QString text = T("Delete the mode file \"%1\"?").arg(QString::fromStdString(m->path));
    if (m->origin == Origin::BuiltInOverridden)
        text += QStringLiteral("\n\n") + T("This file replaces the built-in mode \"%1\"; the built-in mode comes back.").arg(id);
    text += QStringLiteral("\n\n") + T("This cannot be undone.");
    QMessageBox box(QMessageBox::Question, T("Delete"), text, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    const ModeCatalog::Result r = catalog_.remove(m->id);
    if (!r.ok) return showError(T("Could not delete the mode."), r.error);
    populate(id);  // an overridden built-in is selected again, else the first entry
}

void ModesPage::onOpenFolder() {
    std::string err;
    if (!spn::fs::mkdirsPrivate(ctx_.modesDir, &err)) return showError(T("Could not create the modes folder."), err);
    openInEditor(ctx_.modesDir);
}
