// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/modes_page.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFile>
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

#include <QStackedWidget>

#include "pages/mode_editor_widget.hpp"
#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui_i18n.hpp"
#include "unsaved_prompt.hpp"

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
    : QWidget(parent),
      ctx_(ctx),
      ownCatalog_(ctx.modes ? nullptr : new spn::ui::ModeCatalog(ctx.modesDir)),
      catalog_(ctx.modes ? *ctx.modes : *ownCatalog_) {
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
    right_ = new QStackedWidget(split);
    detail_ = new QPlainTextEdit(right_);
    detail_->setReadOnly(true);  // plain text only: mode files are foreign content
    detail_->setAccessibleName(T("Details of the selected mode"));
    right_->addWidget(detail_);
    editor_ = new ModeEditorWidget(ctx_, right_);
    right_->addWidget(editor_);
    connect(editor_, &ModeEditorWidget::closeRequested, this, [this] { right_->setCurrentWidget(detail_); });
    connect(editor_, &ModeEditorWidget::saved, this, [this](const QString &id) {
        catalog_.refresh();
        populate(id);
        right_->setCurrentWidget(editor_);  // stay in the editor after saving
    });
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
    editBtn_ = mk(T("Edit"), T("Edit the mode here: name, channels, buttons and overrides"));
    delBtn_ = mk(T("Delete"), T("Delete the mode file (file modes only)"));
    renameBtn_ = mk(T("Rename..."), T("Give the mode file a new name and id (profiles that use it are listed first)"));
    exportBtn_ = mk(T("Export..."), T("Save a copy of the mode file somewhere else"));
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
    setTabOrder(delBtn_, renameBtn_);
    setTabOrder(renameBtn_, exportBtn_);
    setTabOrder(exportBtn_, folderBtn_);
    setAcceptDrops(true);  // drop .conf files to import them

    connect(list_, &QListWidget::currentItemChanged, this, [this] { onCurrentChanged(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { onEdit(); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { onNew(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this] { onDuplicate(); });
    connect(importBtn_, &QPushButton::clicked, this, [this] { onImport(); });
    connect(editBtn_, &QPushButton::clicked, this, [this] { onEdit(); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { onDelete(); });
    connect(renameBtn_, &QPushButton::clicked, this, [this] { onRename(); });
    connect(exportBtn_, &QPushButton::clicked, this, [this] { onExport(); });
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

QString ModesPage::currentId() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? it->data(kRoleId).toString() : QString();
}

void ModesPage::selectRow(const QString &id) {
    const QSignalBlocker block(list_);
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
}

bool ModesPage::hasUnsavedChanges() const { return right_->currentWidget() == editor_ && editor_->editor().dirty(); }

bool ModesPage::confirmLeaveEditor() {
    if (!hasUnsavedChanges()) return true;
    const spn::ui::ModeForm &f = editor_->editor().form();
    const std::string what = f.name.empty() ? f.id : f.name;
    const bool ok = spn::ui::mayLeaveForm(true, what, [this](const std::string &w) { return unsaved_prompt::ask(this, w); },
                                          [this] { return editor_->save(); });
    if (ok) right_->setCurrentWidget(detail_);  // the editor is left; a discarded form is reloaded on the next open
    return ok;
}

void ModesPage::onCurrentChanged() {
    const QString target = currentId();
    if (hasUnsavedChanges() && target != shownId_) {
        if (!confirmLeaveEditor()) {
            selectRow(shownId_);  // stay on the mode that is being edited
            return;
        }
        selectRow(target);  // a Save repopulated the list on the edited mode
    }
    shownId_ = target;
    right_->setCurrentWidget(detail_);  // the editor belongs to the mode it was opened for
    updateDetail();
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
    // The editor belongs to one mode: a clean one for another mode (the list moved on) makes room for the details.
    if (right_->currentWidget() == editor_ && !editor_->editor().dirty() && qs8(editor_->editor().form().id) != currentId())
        right_->setCurrentWidget(detail_);
    if (right_->currentWidget() != editor_) shownId_ = currentId();
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
        const QStringList uses = usedByLines(QString::fromStdString(m->id));
        t += QLatin1Char('\n');
        if (uses.isEmpty()) {
            t += T("Used by: no profile") + QLatin1Char('\n');
        } else {
            t += T("Used by:") + QLatin1Char('\n');
            for (const QString &u : uses) t += QStringLiteral(" - ") + u + QLatin1Char('\n');
        }
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
    renameBtn_->setEnabled(m && m->origin == Origin::File);
    exportBtn_->setEnabled(m != nullptr);
}

QStringList ModesPage::usedByLines(const QString &id) const {
    QStringList out;
    if (!ctx_.settings) return out;
    for (const ModeCatalog::ModeUse &u : ModeCatalog::usedBy(ctx_.settings->profiles(), id.toStdString())) {
        QString how;
        if (u.selects) how = T("selected as the mode");
        if (!u.buttons.empty()) {
            QStringList nums;
            for (int b : u.buttons) nums << QString::number(b);
            if (!how.isEmpty()) how += QStringLiteral(", ");
            how += T("switched to by button %1").arg(nums.join(QStringLiteral(", ")));
        }
        out << QString::fromStdString(ModeCatalog::useTitle(u)) + QStringLiteral(": ") + how;
    }
    return out;
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
    if (!confirmLeaveEditor()) return;
    const ModeCatalog::Result r = catalog_.createNew(name.toStdString());
    if (!r.ok) return showError(T("Could not create the mode."), r.error);
    populate(QString::fromStdString(r.id));
    openInEditor(r.path);
}

void ModesPage::onDuplicate() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "m".
    const std::string id = m->id;
    const QString label = QString::fromStdString(m->label);
    bool ok = false;
    const QString name = askName(T("Duplicate"), T("Name of the copy:"), T("%1 (copy)").arg(label), &ok);
    if (!ok) return;
    if (!confirmLeaveEditor()) return;
    const ModeCatalog::Result r = catalog_.duplicate(id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the mode."), r.error);
    populate(QString::fromStdString(r.id));
}

void ModesPage::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, T("Import mode file"), QDir::homePath(),
                                                      T("Mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    importPath(file);
}

bool ModesPage::importPath(const QString &file, bool interactive) {
    lastError_.clear();
    const std::string path = file.toStdString();
    std::string name;  // empty: the id comes from the file name
    ModeCatalog::Result chk = catalog_.inspectImport(path, name);
    if (!chk.ok && interactive && chk.error.find("already exists") != std::string::npos) {
        // The id is taken: let the user pick another name.
        bool ok = false;
        const QString n = askName(T("Import mode file"), T("A mode with this id already exists. Name for the imported mode:"),
                                  QFileInfo(file).completeBaseName(), &ok);
        if (!ok) return false;
        name = n.toStdString();
        chk = catalog_.inspectImport(path, name);
    }
    if (!chk.ok) {
        lastError_ = QString::fromStdString(chk.error);
        if (interactive) showError(T("This file cannot be imported."), chk.error);
        return false;
    }

    // Show the warnings and the permanent safety note before anything is kept.
    if (interactive) {
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
        if (box.clickedButton() != yes) return false;
    }

    if (!confirmLeaveEditor()) return;
    const ModeCatalog::Result r = catalog_.importFile(path, name);
    if (!r.ok) {
        lastError_ = QString::fromStdString(r.error);
        if (interactive) showError(T("Could not import the mode."), r.error);
        return false;
    }
    populate(QString::fromStdString(r.id));
    return true;
}

void ModesPage::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

// Dropped files are only read as mode files (same checks and confirmation as Import); nothing is run.
void ModesPage::dropEvent(QDropEvent *e) {
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".conf"), Qt::CaseInsensitive))
            importPath(u.toLocalFile());
    e->acceptProposedAction();
}

bool ModesPage::selectMode(const QString &id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return true;
        }
    return false;
}

bool ModesPage::openEditor() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->path.empty()) return false;
    const std::string id = m->id;
    // Opening reloads the file: ask first if the editor holds edits. A Save refreshes the catalog,
    // so the entry and the definition are looked up again afterwards and copied before the editor
    // (which may show a message box) runs.
    if (!confirmLeaveEditor()) return false;
    const ModeCatalog::ModeEntry *e = catalog_.find(id);
    const spn::ModeDef *def = catalog_.modeSet().find(id);
    if (!e || e->path.empty() || !def) return false;
    const std::string path = e->path;
    const spn::ModeDef defCopy = *def;
    if (!editor_->open(path, defCopy)) return false;
    shownId_ = QString::fromStdString(id);
    right_->setCurrentWidget(editor_);
    return true;
}

void ModesPage::onEdit() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    const std::string sid = m->id;  // copies: "m" does not survive a catalog refresh during a dialog
    if (!m->path.empty()) {
        openEditor();
        return;
    }
    // A built-in mode has no file: offer an editable copy with the same id that replaces it.
    const QString id = QString::fromStdString(sid);
    const QMessageBox::StandardButton a = QMessageBox::question(
        this, T("Edit"),
        T("The built-in mode \"%1\" is not a file. Create an editable file with the same id? It replaces the "
          "built-in mode until you delete the file. To keep the original, use Duplicate instead.")
            .arg(id),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (a != QMessageBox::Yes) return;
    const ModeCatalog::Result r = catalog_.makeEditableCopy(sid);
    if (!r.ok) return showError(T("Could not create the file."), r.error);
    populate(id);
    openEditor();
}

void ModesPage::onDelete() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->origin == Origin::BuiltIn) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "m".
    const std::string sid = m->id;
    const QString id = QString::fromStdString(sid);
    const bool overridden = m->origin == Origin::BuiltInOverridden;
    QString text = T("Delete the mode file \"%1\"?").arg(QString::fromStdString(m->path));
    if (overridden)
        text += QStringLiteral("\n\n") + T("This file replaces the built-in mode \"%1\"; the built-in mode comes back.").arg(id);
    text += QStringLiteral("\n\n") + T("This cannot be undone.");
    QMessageBox box(QMessageBox::Question, T("Delete"), text, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    // Edits of the mode being deleted are moot (the user confirmed); edits of another mode are not.
    if (qs8(editor_->editor().form().id) != id && !confirmLeaveEditor()) return;
    const ModeCatalog::Result r = catalog_.remove(sid);
    if (!r.ok) return showError(T("Could not delete the mode."), r.error);
    right_->setCurrentWidget(detail_);
    populate(id);  // an overridden built-in is selected again, else the first entry
}

bool ModesPage::exportModeTo(const QString &id, const QString &dest) {
    const ModeCatalog::Result r = catalog_.exportTo(id.toStdString(), dest.toStdString());
    lastError_ = QString::fromStdString(r.error);
    return r.ok;
}

void ModesPage::onExport() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    const QString id = QString::fromStdString(m->id);
    // The dialog asks before replacing; the model never overwrites, so an existing file is removed first.
    const QString file = QFileDialog::getSaveFileName(this, T("Export mode file"), QDir::homePath() + QLatin1Char('/') + id + QStringLiteral(".conf"),
                                                      T("Mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    if (QFileInfo::exists(file) && !QFile::remove(file)) return showError(T("Could not export the mode."), file.toStdString());
    if (!exportModeTo(id, file)) showError(T("Could not export the mode."), lastError_.toStdString());
}

bool ModesPage::renameMode(const QString &id, const QString &newName) {
    const ModeCatalog::Result r = catalog_.rename(id.toStdString(), newName.toStdString());
    lastError_ = QString::fromStdString(r.error);
    if (!r.ok) return false;
    populate(QString::fromStdString(r.id));
    return true;
}

void ModesPage::onRename() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->origin != Origin::File) return;
    const QString id = QString::fromStdString(m->id);
    bool ok = false;
    const QString name = askName(T("Rename"), T("New name of the mode:"), QString::fromStdString(m->label), &ok);
    if (!ok) return;
    const std::string nid = ModeCatalog::deriveId(ModeCatalog::cleanLabel(name.toStdString()));
    const QStringList uses = nid == m->id ? QStringList() : usedByLines(id);
    if (!uses.isEmpty()) {
        const QString text = T("The id of the mode changes from \"%1\" to \"%2\". These profiles still refer to the old id and "
                               "will fall back to the default mode until you change them:")
                                 .arg(id, QString::fromStdString(nid)) +
                             QStringLiteral("\n\n - ") + uses.join(QStringLiteral("\n - "));
        QMessageBox box(QMessageBox::Warning, T("Rename"), text, QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(T("Rename"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() != yes) return;
    }
    if (!renameMode(id, name)) showError(T("Could not rename the mode."), lastError_.toStdString());
}

void ModesPage::onOpenFolder() {
    std::string err;
    if (!spn::fs::mkdirsPrivate(ctx_.modesDir, &err)) return showError(T("Could not create the modes folder."), err);
    openInEditor(ctx_.modesDir);
}
