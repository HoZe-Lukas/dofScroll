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

#include "described_item_delegate.hpp"
#include "pages/mode_editor_widget.hpp"
#include "ui/model/mode_info_model.hpp"
#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui_i18n.hpp"
#include "seed.hpp"
#include "ui/model/examples_model.hpp"
#include "ui/model/ui_terms.hpp"

using spn::ui::ModeCatalog;
using Origin = ModeCatalog::Origin;

namespace {
enum { kRoleId = Qt::UserRole };

QString originText(Origin o) {
    QtStrings strings;
    return qs8(spn::ui::modeOriginText(o, strings));
}
}  // namespace

QWidget *makeModesPage(const UiContext &ctx) { return new ModesPage(ctx); }

ModesPage::ModesPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent),
      ctx_(ctx),
      ownCatalog_(ctx.modes ? nullptr : new spn::ui::ModeCatalog(ctx.modesDir)),
      catalog_(ctx.modes ? *ctx.modes : *ownCatalog_) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("<b>") + T(spn::ui::terms::kJoystickModes) + QStringLiteral("</b>"), this);
    root->addWidget(title);
    auto *intro = new QLabel(T("A joystick mode says what the axes and buttons do: scroll, zoom, press keys. Pick one for a "
                               "profile on the Profiles page."), this);
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(intro);

    // Permanent hint: a mode file can press any key (also shortcut chords).
    auto *hint = new QLabel(T("Joystick mode files can press any key. Only use joystick mode files from trusted sources."), this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::PlainText);
    root->addWidget(hint);

    auto *row = new QHBoxLayout;
    auto *split = new QSplitter(Qt::Horizontal, this);
    list_ = new QListWidget(split);
    list_->setAccessibleName(T(spn::ui::terms::kJoystickModes));
    list_->setToolTip(T("All joystick modes: the built-in ones and the files in the joystick modes folder"));
    list_->setItemDelegate(new DescribedItemDelegate(list_));  // the name and, below it, the description
    list_->setWordWrap(true);
    list_->setResizeMode(QListView::Adjust);
    right_ = new QStackedWidget(split);
    detail_ = new QPlainTextEdit(right_);
    detail_->setReadOnly(true);  // plain text only: mode files are foreign content
    detail_->setAccessibleName(T("Details of the selected joystick mode"));
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
    newBtn_ = mk(T("New joystick mode"), T("Create a joystick mode file from a commented template and open it in your editor"));
    exampleBtn_ = mk(T("New from example..."), T("Copy one of the examples that ship with dofscroll into your folder"));
    dupBtn_ = mk(T("Duplicate"), T("Copy the selected joystick mode under a new name"));
    importBtn_ = mk(T("Import file..."), T("Add a joystick mode file from somewhere else (you see its warnings first)"));
    editBtn_ = mk(T("Edit"), T("Edit the joystick mode here: name, channels, buttons and overrides"));
    delBtn_ = mk(T("Delete"), T("Delete the joystick mode file (your own files only)"));
    renameBtn_ = mk(T("Rename..."), T("Give the joystick mode file a new name and id (profiles that use it are listed first)"));
    exportBtn_ = mk(T("Export..."), T("Save a copy of the joystick mode file somewhere else"));
    folderBtn_ = mk(T("Open joystick modes folder"), T("Show the folder with the joystick mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);

    // Tab order follows the creation order: list, details, buttons.
    setTabOrder(list_, detail_);
    setTabOrder(detail_, newBtn_);
    setTabOrder(newBtn_, exampleBtn_);
    setTabOrder(exampleBtn_, dupBtn_);
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
    connect(exampleBtn_, &QPushButton::clicked, this, [this] { onNewFromExample(); });
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

bool ModesPage::pendingEdits() const { return editor_->pending(); }

// The editor writes by itself; before it is replaced or closed the last change is written now.
void ModesPage::flushEditor() { editor_->flush(); }

void ModesPage::onCurrentChanged() {
    const QString target = currentId();
    if (right_->currentWidget() == editor_ && target != shownId_) flushEditor();
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
        it->setData(kRoleOrigin, originText(m.origin));
        it->setData(kRoleDescription, qs8(m.description.empty() ? spn::ui::noDescriptionText(QtStrings()) : m.description));
        if (!m.warnings.empty()) it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        it->setToolTip(qs8(m.description.empty() ? spn::ui::noDescriptionText(QtStrings()) : m.description) + QLatin1Char('\n') +
                       originText(m.origin));
        list_->addItem(it);
        if (id == selectId) select = n;
        ++n;
    }
    if (n) list_->setCurrentRow(select);
    // The editor belongs to one mode: a clean one for another mode (the list moved on) makes room for the details.
    if (right_->currentWidget() == editor_ && qs8(editor_->editor().form().id) != currentId()) right_->setCurrentWidget(detail_);
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
        t += QLatin1Char('\n') + qs8(m->description.empty() ? spn::ui::noDescriptionText(QtStrings()) : m->description) + QLatin1Char('\n');
        if (const spn::ModeDef *def = catalog_.modeSet().find(m->id)) {
            t += QLatin1Char('\n') + T("What it does:") + QLatin1Char('\n');
            for (const std::string &line : spn::ui::joystickModeChannelLines(*def, QtStrings())) t += QStringLiteral(" - ") + qs8(line) + QLatin1Char('\n');
        }
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
        t += QLatin1Char('\n') + T("Warnings for the joystick modes folder:") + QLatin1Char('\n');
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
        if (u.selects) how = T("selected as the joystick mode");
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
    QMessageBox::warning(this, T(spn::ui::terms::kJoystickModes), what + QStringLiteral("\n\n") + QString::fromStdString(detail));
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
    const QString name = askName(T("New joystick mode"), T("Name of the new joystick mode:"), QString(), &ok);
    if (!ok) return;
    flushEditor();
    const ModeCatalog::Result r = catalog_.createNew(name.toStdString());
    if (!r.ok) return showError(T("Could not create the joystick mode."), r.error);
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
    flushEditor();
    const ModeCatalog::Result r = catalog_.duplicate(id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the joystick mode."), r.error);
    populate(QString::fromStdString(r.id));
}

void ModesPage::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, T("Import joystick mode file"), QDir::homePath(),
                                                      T("Joystick mode files (*.conf);;All files (*)"));
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
        const QString n = askName(T("Import joystick mode file"),
                                  T("A joystick mode with this id already exists. Name for the imported joystick mode:"),
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
        QString text = T("Import the joystick mode \"%1\"?").arg(QString::fromStdString(chk.id));
        if (!chk.warnings.empty()) {
            text += QStringLiteral("\n\n") + T("Warnings:");
            for (const std::string &w : chk.warnings) text += QStringLiteral("\n - ") + QString::fromStdString(w);
        } else {
            text += QStringLiteral("\n\n") + T("The file has no loader warnings.");
        }
        QMessageBox box(QMessageBox::Warning, T("Import joystick mode file"), text, QMessageBox::NoButton, this);
        box.setInformativeText(QString::fromStdString(ModeCatalog::importSafetyNote(QtStrings())));
        QPushButton *yes = box.addButton(T("Import"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() != yes) return false;
    }

    flushEditor();
    const ModeCatalog::Result r = catalog_.importFile(path, name);
    if (!r.ok) {
        lastError_ = QString::fromStdString(r.error);
        if (interactive) showError(T("Could not import the joystick mode."), r.error);
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
    // Opening reloads the file: the edits of the editor are written first. A write refreshes the
    // catalog, so the entry and the definition are looked up again afterwards and copied before the
    // editor (which may show a message box) runs.
    flushEditor();
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
        T("The built-in joystick mode \"%1\" is not a file. Create an editable file with the same id? It replaces the "
          "built-in joystick mode until you delete the file. To keep the original, use Duplicate instead.")
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
    QString text = T("Delete the joystick mode file \"%1\"?").arg(QString::fromStdString(m->path));
    if (overridden)
        text += QStringLiteral("\n\n") + T("This file replaces the built-in joystick mode \"%1\"; the built-in one comes back.").arg(id);
    text += QStringLiteral("\n\n") + T("This cannot be undone.");
    QMessageBox box(QMessageBox::Question, T("Delete"), text, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    // Edits of the joystick mode being deleted are moot (the user confirmed); edits of another one are written.
    if (qs8(editor_->editor().form().id) != id) flushEditor();
    else right_->setCurrentWidget(detail_);
    const ModeCatalog::Result r = catalog_.remove(sid);
    if (!r.ok) return showError(T("Could not delete the joystick mode."), r.error);
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
    const QString file = QFileDialog::getSaveFileName(this, T("Export joystick mode file"),
                                                      QDir::homePath() + QLatin1Char('/') + id + QStringLiteral(".conf"),
                                                      T("Joystick mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    if (QFileInfo::exists(file) && !QFile::remove(file)) return showError(T("Could not export the joystick mode."), file.toStdString());
    if (!exportModeTo(id, file)) showError(T("Could not export the joystick mode."), lastError_.toStdString());
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
    const QString name = askName(T("Rename"), T("New name of the joystick mode:"), QString::fromStdString(m->label), &ok);
    if (!ok) return;
    const std::string nid = ModeCatalog::deriveId(ModeCatalog::cleanLabel(name.toStdString()));
    const QStringList uses = nid == m->id ? QStringList() : usedByLines(id);
    if (!uses.isEmpty()) {
        const QString text = T("The id of the joystick mode changes from \"%1\" to \"%2\". These profiles still refer to the "
                               "old id and will fall back to the Global joystick mode until you change them:")
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
    if (!renameMode(id, name)) showError(T("Could not rename the joystick mode."), lastError_.toStdString());
}

void ModesPage::onOpenFolder() {
    std::string err;
    if (!spn::fs::mkdirsPrivate(ctx_.modesDir, &err)) return showError(T("Could not create the joystick modes folder."), err);
    openInEditor(ctx_.modesDir);
}

// "New from example...": the examples that ship with dofscroll and are not in the folder yet; the chosen
// one is copied (checked like an import) and selected.
void ModesPage::onNewFromExample() {
    const std::vector<spn::ui::Example> ex = spn::ui::availableModeExamples(catalog_, spn::examplesDir());
    if (ex.empty()) {
        QMessageBox::information(this, T("New from example"),
                                 T("All examples are already in your joystick modes folder, or none ships with this installation."));
        return;
    }
    QtStrings strings;
    QStringList items;
    for (const spn::ui::Example &e : ex)
        items << qs8(e.label) + QStringLiteral("  -  ") + qs8(spn::ui::exampleOriginText(strings)) +
                     (e.description.empty() ? QString() : QStringLiteral("  -  ") + qs8(e.description));
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, T("New from example"), T("Copy this example into your joystick modes folder:"),
                                               items, 0, false, &ok);
    if (!ok) return;
    const int idx = items.indexOf(pick);
    if (idx < 0) return;
    if (!installExample(QString::fromStdString(ex[size_t(idx)].id))) showError(T("Could not add the example."), lastError_.toStdString());
}

// Copies the example with this id (no dialog); false + lastError() if it is not offered or cannot be copied.
bool ModesPage::installExample(const QString &id) {
    lastError_.clear();
    for (const spn::ui::Example &e : spn::ui::availableModeExamples(catalog_, spn::examplesDir())) {
        if (qs8(e.id) != id) continue;
        flushEditor();
        const spn::ui::ExampleResult r = spn::ui::installModeExample(catalog_, e);
        if (!r.ok) {
            lastError_ = qs8(r.error);
            return false;
        }
        populate(qs8(r.id));
        return true;
    }
    lastError_ = T("This example is not offered.");
    return false;
}

QStringList ModesPage::exampleIds() const {
    QStringList out;
    for (const spn::ui::Example &e : spn::ui::availableModeExamples(catalog_, spn::examplesDir())) out << qs8(e.id);
    return out;
}

QString ModesPage::originOf(const QString &id) const {
    const ModeCatalog::ModeEntry *e = catalog_.find(id.toStdString());
    return e ? originText(e->origin) : QString();
}
