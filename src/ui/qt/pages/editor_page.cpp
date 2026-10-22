// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/editor_page.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "pages/mode_editor_widget.hpp"
#include "ui/model/ui_terms.hpp"
#include "ui_i18n.hpp"

using namespace spn::ui;

QWidget *createEditorPage(const UiContext &ctx) { return new EditorPage(ctx); }

EditorPage::EditorPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent),
      ctx_(ctx),
      ownCatalog_(ctx.modes ? nullptr : new ModeCatalog(ctx.modesDir)),
      catalog_(ctx.modes ? *ctx.modes : *ownCatalog_) {
    auto *root = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("<b>") + T("Editor") + QStringLiteral("</b>"), this);
    title->setToolTip(T("Click a motion of the cap or a button, then choose what it does. Every change is saved at once."));
    auto *label = new QLabel(T("Joystick mode:"), this);
    combo_ = new QComboBox(this);
    combo_->setAccessibleName(T("Joystick mode to edit"));
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo_->setMinimumContentsLength(12);
    combo_->setMaximumWidth(420);
    label->setBuddy(combo_);
    copy_ = new QPushButton(T("Make my own copy"), this);
    copy_->setToolTip(T("A built-in joystick mode is not a file. Make a copy with its own name and edit that."));
    row->addWidget(title);
    row->addSpacing(24);
    row->addWidget(label);
    row->addWidget(combo_);
    row->addWidget(copy_);
    row->addStretch(1);
    root->addLayout(row);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::PlainText);
    note_->setAccessibleName(T("About the chosen joystick mode"));
    root->addWidget(note_);

    editor_ = new ModeEditorWidget(ctx_, this);
    editor_->setCompact(true);
    root->addWidget(editor_, 100);
    root->addStretch(1);  // takes the room while a built-in joystick mode hides the editor

    connect(combo_, &QComboBox::activated, this, [this] { openCurrent(); });
    connect(copy_, &QPushButton::clicked, this, [this] { makeCopy(); });
    populate(QString());
    openCurrent();
}

const ModeCatalog::ModeEntry *EditorPage::current() const {
    const QString id = combo_->currentData().toString();
    return id.isEmpty() ? nullptr : catalog_.find(id.toStdString());
}

QString EditorPage::currentId() const { return combo_->currentData().toString(); }
bool EditorPage::canEdit() const {
    const ModeCatalog::ModeEntry *e = current();
    return e && !e->path.empty();
}
QString EditorPage::note() const { return note_->text(); }
void EditorPage::flush() { editor_->flush(); }

void EditorPage::populate(const QString &selectId) {
    const QString keep = selectId.isEmpty() ? currentId() : selectId;
    QSignalBlocker block(combo_);
    combo_->clear();
    for (const ModeCatalog::ModeEntry &m : catalog_.modes()) {
        const QString name = qs8(m.label.empty() ? m.id : m.label);
        combo_->addItem(name + (m.path.empty() ? QString() : QStringLiteral(" *")), qs8(m.id));
    }
    int idx = combo_->findData(keep);
    if (idx < 0) idx = combo_->findData(QStringLiteral("normal"));
    combo_->setCurrentIndex(std::max(0, idx));
}

bool EditorPage::selectMode(const QString &id) {
    const int idx = combo_->findData(id);
    if (idx < 0) return false;
    combo_->setCurrentIndex(idx);
    openCurrent();
    return true;
}

void EditorPage::openCurrent() {
    editor_->flush();  // the mode that was edited before is written first
    const ModeCatalog::ModeEntry *e = current();
    if (!e) return;
    const std::string id = e->id, path = e->path;
    const spn::ModeDef *def = catalog_.modeSet().find(id);
    const bool file = !path.empty();
    copy_->setVisible(!file);
    editor_->setVisible(file);
    if (!file || !def) {
        note_->setText(T("This is a built-in joystick mode, so it cannot be changed. Make your own copy to edit it."));
        note_->show();
        shown_.clear();
        return;
    }
    note_->setText(e->origin == ModeCatalog::Origin::BuiltInOverridden ? T("Your file replaces the built-in joystick mode of this name.") : QString());
    note_->setVisible(!note_->text().isEmpty());
    if (editor_->open(path, *def)) shown_ = qs8(id);
}

bool EditorPage::makeCopy() {
    const ModeCatalog::ModeEntry *e = current();
    if (!e || !e->path.empty()) return false;
    const std::string name = (e->label.empty() ? e->id : e->label) + " (mine)";
    const ModeCatalog::Result r = catalog_.duplicate(e->id, name);
    if (!r.ok) {
        QMessageBox::warning(this, T("Editor"), T("Could not create the file.") + QStringLiteral("\n\n") + qs8(r.error));
        return false;
    }
    populate(qs8(r.id));
    openCurrent();
    return true;
}

void EditorPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    // the list of joystick modes may have changed on the Joystick modes page
    if (!catalog_.refreshIfChanged()) return;
    populate(QString());
    openCurrent();
}

void EditorPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    editor_->flush();  // leaving the page writes the last change
}
