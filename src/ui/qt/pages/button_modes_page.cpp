// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/button_modes_page.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "key_combo.hpp"
#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui_i18n.hpp"
#include "unsaved_prompt.hpp"

using spn::ui::Binding;
using spn::ui::BindingKind;
using spn::ui::ButtonModeCatalog;

namespace {
enum { kRoleId = Qt::UserRole };

QString qs(const std::string &s) { return QString::fromUtf8(s.c_str(), int(s.size())); }
}  // namespace

QWidget *createButtonModesPage(const UiContext &ctx) { return new ButtonModesPage(ctx); }

ButtonModesPage::ButtonModesPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent),
      ctx_(ctx),
      ownCatalog_(ctx.buttonModes ? nullptr : new ButtonModeCatalog(ctx.buttonModesDir)),
      catalog_(ctx.buttonModes ? *ctx.buttonModes : *ownCatalog_) {
    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("<b>") + T("Button modes") + QStringLiteral("</b>"), this));

    auto addNote = [&](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        root->addWidget(l);
    };
    addNote(T("A button mode is a named button behavior. Pick it for any button on the Profiles page; the same "
              "button mode can be used on several buttons."));
    addNote(T("A button mode can press a key or click. Only use button modes from trusted sources. An action that is "
              "the same everywhere and always is better set in the vendor driver (see docs/modes.md)."));

    auto *row = new QHBoxLayout;
    auto *split = new QSplitter(Qt::Horizontal, this);
    list_ = new QListWidget(split);
    list_->setAccessibleName(T("Button modes"));
    list_->setToolTip(T("The files in the button modes folder"));

    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout;
    name_ = new QLineEdit(right);
    name_->setAccessibleName(T("Name"));
    name_->setToolTip(T("Shown in the button drop-downs"));
    description_ = new QLineEdit(right);
    description_->setAccessibleName(T("Description"));
    kind_ = new QComboBox(right);
    kind_->setAccessibleName(T("Action"));
    for (const spn::ui::BindingKindInfo &k : spn::ui::bindingKindsFor(spn::ui::BindingContext::Specific, false))
        kind_->addItem(T(k.label), int(k.kind));
    argHost_ = new QWidget(right);
    auto *ah = new QHBoxLayout(argHost_);
    ah->setContentsMargins(0, 0, 0, 0);
    auto *actionRow = new QWidget(right);
    auto *arl = new QHBoxLayout(actionRow);
    arl->setContentsMargins(0, 0, 0, 0);
    arl->addWidget(kind_, 1);
    arl->addWidget(argHost_, 1);
    form->addRow(T("Name:"), name_);
    form->addRow(T("Description:"), description_);
    form->addRow(T("Action:"), actionRow);
    fileLabel_ = new QLabel(right);
    fileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLabel_->setWordWrap(true);
    form->addRow(T("File:"), fileLabel_);
    rl->addLayout(form);
    auto *editRow = new QHBoxLayout;
    saveBtn_ = new QPushButton(T("Save"), right);
    saveBtn_->setToolTip(T("Write the changes to the button mode file"));
    revertBtn_ = new QPushButton(T("Revert"), right);
    revertBtn_->setToolTip(T("Discard the changes and reload the file"));
    textEditorBtn_ = new QPushButton(T("Open in text editor"), right);
    textEditorBtn_->setToolTip(T("Open the file in your default editor"));
    editRow->addWidget(saveBtn_);
    editRow->addWidget(revertBtn_);
    editRow->addWidget(textEditorBtn_);
    editRow->addStretch(1);
    rl->addLayout(editRow);
    warnings_ = new QPlainTextEdit(right);
    warnings_->setReadOnly(true);  // plain text only: button mode files are foreign content
    warnings_->setAccessibleName(T("Warnings of the selected button mode"));
    warnings_->setMaximumHeight(160);
    rl->addWidget(warnings_);
    rl->addStretch(1);
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
    newBtn_ = mk(T("New button mode"), T("Create a button mode file from a commented template"));
    dupBtn_ = mk(T("Duplicate"), T("Copy the selected button mode under a new name"));
    importBtn_ = mk(T("Import file..."), T("Add a button mode file from somewhere else (you see its warnings first)"));
    delBtn_ = mk(T("Delete"), T("Delete the button mode file"));
    folderBtn_ = mk(T("Open button modes folder"), T("Show the folder with the button mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);

    setTabOrder(list_, name_);
    setTabOrder(name_, description_);
    setTabOrder(description_, kind_);
    setTabOrder(kind_, saveBtn_);
    setTabOrder(saveBtn_, revertBtn_);
    setTabOrder(revertBtn_, textEditorBtn_);
    setTabOrder(textEditorBtn_, newBtn_);
    setTabOrder(newBtn_, dupBtn_);
    setTabOrder(dupBtn_, importBtn_);
    setTabOrder(importBtn_, delBtn_);
    setTabOrder(delBtn_, folderBtn_);

    connect(list_, &QListWidget::currentItemChanged, this, [this] { onCurrentChanged(); });
    connect(name_, &QLineEdit::textEdited, this, [this] { markDirty(); });
    connect(description_, &QLineEdit::textEdited, this, [this] { markDirty(); });
    connect(kind_, &QComboBox::activated, this, [this](int idx) {
        const std::vector<std::string> ids = spn::ui::bindingModeIds(spn::loadModes(ctx_.modesDir, nullptr));
        binding_ = spn::ui::withKind(binding_, BindingKind(kind_->itemData(idx).toInt()), ids);
        rebuildActionArg();
        markDirty();
    });
    connect(saveBtn_, &QPushButton::clicked, this, [this] { onSave(); });
    connect(revertBtn_, &QPushButton::clicked, this, [this] { loadForm(); });
    connect(textEditorBtn_, &QPushButton::clicked, this, [this] { onOpenInEditor(); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { onNew(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this] { onDuplicate(); });
    connect(importBtn_, &QPushButton::clicked, this, [this] { onImport(); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { onDelete(); });
    connect(folderBtn_, &QPushButton::clicked, this, [this] { onOpenFolder(); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ButtonModesPage::refreshIfChanged);

    populate(QString());
}

int ButtonModesPage::buttonModeCount() const { return list_->count(); }

void ButtonModesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refreshIfChanged();
    timer_->start();
}

void ButtonModesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
}

QString ButtonModesPage::currentId() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? it->data(kRoleId).toString() : QString();
}

void ButtonModesPage::selectRow(const QString &id) {
    const QSignalBlocker block(list_);
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
}

// True if the form may be replaced: nothing to lose, or the user chose Save (and it worked) or Discard.
// dirty_ stays set; the caller resets it once the replacement really happens.
bool ButtonModesPage::confirmLeaveForm() {
    if (!dirty_) return true;
    const std::string what = (name_->text().isEmpty() ? shownId_ : name_->text()).toStdString();
    return spn::ui::mayLeaveForm(true, what, [this](const std::string &w) { return unsaved_prompt::ask(this, w); },
                                 [this] { return save(shownId_.toStdString()); });
}

void ButtonModesPage::onCurrentChanged() {
    const QString target = currentId();
    if (dirty_ && target != shownId_) {
        if (!confirmLeaveForm()) {
            selectRow(shownId_);  // stay on the button mode that is being edited
            return;
        }
        selectRow(target);  // a Save repopulated the list on the edited entry
    }
    loadForm();
}

const ButtonModeCatalog::Entry *ButtonModesPage::current() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? catalog_.find(it->data(kRoleId).toString().toStdString()) : nullptr;
}

void ButtonModesPage::refreshIfChanged() {
    const ButtonModeCatalog::Entry *cur = current();
    const QString keep = cur ? qs(cur->id) : QString();
    if (!catalog_.refreshIfChanged()) return;
    // Unsaved edits stay in the form; the list and the warnings follow the folder.
    populate(keep);
}

void ButtonModesPage::populate(const QString &selectId) {
    {
        const QSignalBlocker block(list_);
        list_->clear();
        int select = 0, n = 0;
        for (const ButtonModeCatalog::Entry &b : catalog_.buttonModes()) {
            const QString id = qs(b.id), label = qs(b.label);
            auto *it = new QListWidgetItem(label == id ? id : label + QStringLiteral(" (") + id + QStringLiteral(")"));
            it->setData(kRoleId, id);
            if (!b.warnings.empty()) it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
            it->setToolTip(qs(b.description));
            list_->addItem(it);
            if (id == selectId) select = n;
            ++n;
        }
        if (n) list_->setCurrentRow(select);
    }
    if (dirty_ && current() && qs(current()->id) == selectId) {
        // keep the edited form, only refresh the warnings
        warnings_->setPlainText(QString());
    } else {
        loadForm();
    }
}

void ButtonModesPage::loadForm() {
    loading_ = true;
    const ButtonModeCatalog::Entry *e = current();
    name_->setText(e ? qs(e->label) : QString());
    description_->setText(e ? qs(e->description) : QString());
    binding_ = Binding();
    if (e) spn::ui::parseBinding(e->action, &binding_);
    int idx = 0;
    for (int i = 0; i < kind_->count(); ++i)
        if (kind_->itemData(i).toInt() == int(binding_.kind)) idx = i;
    kind_->setCurrentIndex(idx);
    rebuildActionArg();
    fileLabel_->setText(e ? qs(e->path) : QString());
    shownId_ = e ? qs(e->id) : QString();
    QString t;
    if (e && !e->warnings.empty()) {
        t += T("Warnings:") + QLatin1Char('\n');
        for (const std::string &w : e->warnings) t += QStringLiteral(" - ") + qs(w) + QLatin1Char('\n');
    }
    if (!catalog_.generalWarnings().empty()) {
        t += QLatin1Char('\n') + T("Warnings for the button modes folder:") + QLatin1Char('\n');
        for (const std::string &w : catalog_.generalWarnings()) t += QStringLiteral(" - ") + qs(w) + QLatin1Char('\n');
    }
    if (!e && catalog_.buttonModes().empty())
        t = T("No button modes yet. \"New button mode\" creates one; the folder also accepts files you copy there.");
    warnings_->setPlainText(t);
    const bool has = e != nullptr;
    for (QWidget *w : {static_cast<QWidget *>(name_), static_cast<QWidget *>(description_), static_cast<QWidget *>(kind_),
                       static_cast<QWidget *>(argHost_), static_cast<QWidget *>(textEditorBtn_)})
        w->setEnabled(has);
    dupBtn_->setEnabled(has);
    delBtn_->setEnabled(has);
    dirty_ = false;
    saveBtn_->setEnabled(false);
    revertBtn_->setEnabled(false);
    loading_ = false;
}

void ButtonModesPage::rebuildActionArg() {
    delete argWidget_;
    argWidget_ = nullptr;
    auto *lay = argHost_->layout();
    if (binding_.kind == BindingKind::Key) {
        auto *kc = makeKeyCombo(qs(binding_.arg), argHost_);
        kc->setAccessibleName(T("Key"));
        connect(kc, &QComboBox::activated, this, [this, kc](int) {
            if (!kc->currentData().isValid()) return;
            binding_.arg = kc->currentData().toString().toStdString();
            markDirty();
        });
        argWidget_ = kc;
    } else if (binding_.kind == BindingKind::Mode || binding_.kind == BindingKind::ToggleMode ||
               binding_.kind == BindingKind::HoldMode) {
        auto *mc = new QComboBox(argHost_);
        mc->setAccessibleName(T("Mode"));
        std::vector<std::string> ids = spn::ui::bindingModeIds(spn::loadModes(ctx_.modesDir, nullptr));
        if (!binding_.arg.empty() && std::find(ids.begin(), ids.end(), binding_.arg) == ids.end()) ids.push_back(binding_.arg);
        for (const std::string &id : ids) mc->addItem(qs(id), qs(id));
        mc->setCurrentIndex(std::max(0, mc->findData(qs(binding_.arg))));
        connect(mc, &QComboBox::activated, this, [this, mc](int) {
            binding_.arg = mc->currentData().toString().toStdString();
            markDirty();
        });
        argWidget_ = mc;
    }
    if (argWidget_) lay->addWidget(argWidget_);
}

void ButtonModesPage::markDirty() {
    if (loading_) return;
    dirty_ = true;
    saveBtn_->setEnabled(true);
    revertBtn_->setEnabled(true);
}

std::string ButtonModesPage::composedAction() const { return spn::ui::composeBinding(binding_); }

void ButtonModesPage::showError(const QString &what, const std::string &detail) {
    QMessageBox::warning(this, T("Button modes"), what + QStringLiteral("\n\n") + qs(detail));
}

bool ButtonModesPage::openInEditor(const std::string &path) {
    std::string err;
    if (ctx_.runner && ctx_.runner->openPath(path, &err)) return true;
    showError(T("Could not open the file."), err);
    return false;
}

QString ButtonModesPage::askName(const QString &title, const QString &label, const QString &initial, bool *ok) {
    return QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, ok).trimmed();
}

bool ButtonModesPage::selectButtonMode(const QString &id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return true;
        }
    return false;
}

bool ButtonModesPage::stageEdit(const QString &name, const QString &description, const QString &action) {
    if (!current()) return false;
    name_->setText(name);
    description_->setText(description);
    Binding b;
    if (!spn::ui::parseBinding(action.toStdString(), &b)) return false;
    binding_ = b;
    int idx = 0;
    for (int i = 0; i < kind_->count(); ++i)
        if (kind_->itemData(i).toInt() == int(b.kind)) idx = i;
    kind_->setCurrentIndex(idx);
    rebuildActionArg();
    loading_ = false;
    markDirty();
    return true;
}

bool ButtonModesPage::editCurrent(const QString &name, const QString &description, const QString &action) {
    if (!stageEdit(name, description, action)) return false;
    const std::string id = current()->id;
    save(id);
    return !dirty_ && catalog_.find(id) != nullptr;
}

void ButtonModesPage::onSave() { save(currentId().toStdString()); }

// Saves the form under `id`: the entry the form belongs to, which is not the selected row while the
// selection is being moved to another entry.
bool ButtonModesPage::save(const std::string &id) {
    if (id.empty() || !catalog_.find(id)) return false;
    const std::string action = composedAction();
    if (action.empty()) {
        showError(T("Could not save the button mode."), "choose a key or a mode first");
        return false;
    }
    const ButtonModeCatalog::Result r =
        catalog_.save(id, name_->text().toUtf8().toStdString(), description_->text().toUtf8().toStdString(), action);
    if (!r.ok) {
        showError(T("Could not save the button mode."), r.error);
        return false;
    }
    dirty_ = false;
    populate(qs(id));
    return true;
}

void ButtonModesPage::onNew() {
    bool ok = false;
    const QString name = askName(T("New button mode"), T("Name of the new button mode:"), QString(), &ok);
    if (!ok) return;
    if (!confirmLeaveForm()) return;
    const ButtonModeCatalog::Result r = catalog_.createNew(name.toStdString());
    if (!r.ok) return showError(T("Could not create the button mode."), r.error);
    dirty_ = false;
    populate(qs(r.id));
}

void ButtonModesPage::onDuplicate() {
    const ButtonModeCatalog::Entry *e = current();
    if (!e) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "e".
    const std::string id = e->id;
    const QString label = qs(e->label);
    bool ok = false;
    const QString name = askName(T("Duplicate"), T("Name of the copy:"), T("%1 (copy)").arg(label), &ok);
    if (!ok) return;
    if (!confirmLeaveForm()) return;
    const ButtonModeCatalog::Result r = catalog_.duplicate(id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the button mode."), r.error);
    dirty_ = false;
    populate(qs(r.id));
}

void ButtonModesPage::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, T("Import button mode file"), QDir::homePath(),
                                                      T("Button mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    const std::string path = file.toStdString();
    std::string name;
    ButtonModeCatalog::Result chk = catalog_.inspectImport(path, name);
    if (!chk.ok && chk.error.find("already exists") != std::string::npos) {
        bool ok = false;
        const QString n = askName(T("Import button mode file"),
                                  T("A button mode with this id already exists. Name for the imported button mode:"),
                                  QFileInfo(file).completeBaseName(), &ok);
        if (!ok) return;
        name = n.toStdString();
        chk = catalog_.inspectImport(path, name);
    }
    if (!chk.ok) return showError(T("This file cannot be imported."), chk.error);

    QString text = T("Import the button mode \"%1\"?").arg(qs(chk.id));
    if (!chk.warnings.empty()) {
        text += QStringLiteral("\n\n") + T("Warnings:");
        for (const std::string &w : chk.warnings) text += QStringLiteral("\n - ") + qs(w);
    } else {
        text += QStringLiteral("\n\n") + T("The file has no loader warnings.");
    }
    QMessageBox box(QMessageBox::Warning, T("Import button mode file"), text, QMessageBox::NoButton, this);
    box.setInformativeText(qs(ButtonModeCatalog::importSafetyNote(QtStrings())));
    QPushButton *yes = box.addButton(T("Import"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    if (!confirmLeaveForm()) return;
    const ButtonModeCatalog::Result r = catalog_.importFile(path, name);
    if (!r.ok) return showError(T("Could not import the button mode."), r.error);
    dirty_ = false;
    populate(qs(r.id));
}

void ButtonModesPage::onDelete() {
    const ButtonModeCatalog::Entry *e = current();
    if (!e) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "e".
    const std::string id = e->id;
    QString text = T("Delete the button mode file \"%1\"?").arg(qs(e->path));
    if (ctx_.settings && ctx_.modes) {
        const std::vector<std::string> refs = catalog_.referencedBy(id, *ctx_.settings, ctx_.modes->modeSet());
        if (!refs.empty()) {
            text += QStringLiteral("\n\n") + T("It is still used by:");
            for (const std::string &r : refs) text += QStringLiteral("\n - ") + qs(r);
            text += QStringLiteral("\n") + T("Those buttons will show \"missing: %1\" and do nothing.").arg(qs(id));
        }
    }
    text += QStringLiteral("\n\n") + T("This cannot be undone.");
    QMessageBox box(QMessageBox::Question, T("Delete"), text, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    const ButtonModeCatalog::Result r = catalog_.remove(id);
    if (!r.ok) return showError(T("Could not delete the button mode."), r.error);
    dirty_ = false;
    populate(QString());
}

void ButtonModesPage::onOpenFolder() {
    std::string err;
    if (!spn::fs::mkdirsPrivate(ctx_.buttonModesDir, &err))
        return showError(T("Could not create the button modes folder."), err);
    openInEditor(ctx_.buttonModesDir);
}

void ButtonModesPage::onOpenInEditor() {
    const ButtonModeCatalog::Entry *e = current();
    if (!e) return;
    const std::string path = e->path;
    openInEditor(path);
}
