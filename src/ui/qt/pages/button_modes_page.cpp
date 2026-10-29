// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/button_modes_page.hpp"
#include "pages/button_use_widget.hpp"

#include <memory>
#include <QComboBox>
#include <cstdio>
#include <QDir>
#include <QFileDialog>
#include <QFile>
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
#include <QScrollArea>
#include <QSplitter>

#include "split_defaults.hpp"
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "described_item_delegate.hpp"
#include "pages/cycle_editor_widget.hpp"
#include "key_combo.hpp"
#include "motion_view.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/cycle_editor_model.hpp"
#include "ui/model/mode_info_model.hpp"
#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/ui_links.hpp"
#include "ui_i18n.hpp"
#include "seed.hpp"
#include "ui/model/examples_model.hpp"
#include "ui/model/ui_terms.hpp"
#include "plain_text.hpp"

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
    root->addWidget(new QLabel(QStringLiteral("<b>") + T(spn::ui::terms::kButtonModes) + QStringLiteral("</b>"), this));

    auto addNote = [&](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        root->addWidget(l);
    };
    addNote(T("A button mode is a named button behavior. Pick it for any button on the Profiles page; the same "
              "button mode can be used on several buttons."));
    addNote(T("A button mode can press a key or click. Only use button modes from trusted sources. An action that is "
              "the same everywhere and always is better set in the vendor driver (see the documentation)."));
    {
        auto *docs = new QPushButton(T("Documentation"), this);
        docs->setObjectName(QStringLiteral("documentation"));
        docs->setToolTip(T("Open the documentation of the interface in your browser"));
        connect(docs, &QPushButton::clicked, this, [this] {
            std::string err;
            if (ctx_.runner) spn::ui::openDocs(*ctx_.runner, "modes.md", &err);
        });
        root->addWidget(docs, 0, Qt::AlignLeft);
    }

    auto *row = new QHBoxLayout;
    auto *split = new QSplitter(Qt::Horizontal, this);
    list_ = new QListWidget(split);
    list_->setAccessibleName(T(spn::ui::terms::kButtonModes));
    list_->setToolTip(T("The files in the button modes folder"));
    list_->setItemDelegate(new DescribedItemDelegate(list_));  // the name and, below it, the description
    list_->setWordWrap(true);
    list_->setResizeMode(QListView::Adjust);

    // The editor column scrolls: with the cycle section it is taller than the default window on platforms with
    // larger fonts, and the page must fit the default window (the default size self-test).
    auto *rightScroll = new QScrollArea(split);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setFocusPolicy(Qt::NoFocus);  // the controls inside take the focus, not the area (keyboard self-test: Tab order)
    auto *right = new QWidget;
    rightScroll->setWidget(right);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(spn::ui::tokens::kRowGap, 0, 0, 0);  // a gap to the list
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);  // the labels above the fields: a label column would widen the narrow editor column
    name_ = new QLineEdit(right);
    name_->setAccessibleName(T("Name"));
    name_->setToolTip(T("Shown in the button drop-downs"));
    description_ = new QLineEdit(right);
    description_->setAccessibleName(T("Description"));
    description_->setPlaceholderText(T("No description"));
    kind_ = new QComboBox(right);
    kind_->setAccessibleName(T("Action"));
    kind_->setAccessibleDescription(T("Action"));  // the name of a drop-down is its current text
    for (const spn::ui::BindingKindInfo &k : spn::ui::bindingKindsFor(spn::ui::BindingContext::Specific, false))
        kind_->addItem(T(k.label), int(k.kind));
    // The width of the page must not follow the longest action text (platform fonts differ a lot):
    // a fixed number of characters, the list opens wide enough for every entry.
    kind_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    kind_->setMinimumContentsLength(8);  // the editor column scrolls but must fit the default window without a horizontal bar
    argHost_ = new QWidget(right);
    auto *ah = new QHBoxLayout(argHost_);
    ah->setContentsMargins(0, 0, 0, 0);
    auto *actionRow = new QWidget(right);
    auto *arl = new QVBoxLayout(actionRow);  // the argument (key combination, mode, ...) below the action:
    arl->setContentsMargins(0, 0, 0, 0);     // side by side it makes the page wider than a window
    arl->setSpacing(spn::ui::tokens::kRowGap);
    // what a press does, as the same chip as on the other pages (button_chip_model.hpp)
    actionChip_ = new motionview::ButtonChips(right);
    actionChip_->setObjectName(QStringLiteral("actionChip"));
    arl->addWidget(actionChip_);
    arl->addWidget(kind_);
    arl->addWidget(argHost_);
    form->addRow(T("Name:"), name_);
    form->addRow(T("Description:"), description_);
    form->addRow(T("Action:"), actionRow);
    fileLabel_ = new QLabel(right);
    fileLabel_->setTextFormat(Qt::PlainText);
    fileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLabel_->setWordWrap(true);
    form->addRow(T("File:"), fileLabel_);
    rl->addLayout(form);
    cycle_ = new CycleEditorWidget(right);
    cycle_->setVisible(false);
    rl->addWidget(cycle_);
    {   // the device buttons of the Global profile: bind the selected button mode to one of them
        auto *useBox = new QGroupBox(T("Use it on a button"), right);
        auto *ul = new QVBoxLayout(useBox);
        use_ = new ButtonUseWidget(ctx_, useBox);
        ul->addWidget(use_);
        rl->addWidget(useBox);
    }
    problem_ = new QLabel(right);
    problem_->setTextFormat(Qt::PlainText);
    problem_->setWordWrap(true);
    problem_->setProperty("error", true);
    problem_->setVisible(false);
    problem_->setAccessibleName(T("Problem"));
    rl->addWidget(problem_);
    textEditorBtn_ = new QPushButton(T("Open in text editor"), right);
    textEditorBtn_->setToolTip(T("Open the file in your default editor"));
    rl->addWidget(textEditorBtn_, 0, Qt::AlignLeft);
    warnings_ = new QPlainTextEdit(right);
    warnings_->setReadOnly(true);  // plain text only: button mode files are foreign content
    warnings_->setAccessibleName(T("Warnings of the selected button mode"));
    warnings_->setMaximumHeight(160);
    rl->addWidget(warnings_);
    rl->addStretch(1);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    split->setSizes({300, 700});
    new spn::ui::SplitDefaults(split, 30, 260);  // the list would otherwise follow its longest description and squeeze the editor
    row->addWidget(split, 1);

    auto *buttons = new QVBoxLayout;
    auto mk = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    newBtn_ = mk(T("New button mode"), T("Create a button mode file from a commented template"));
    exampleBtn_ = mk(T("New from example..."), T("Copy one of the examples that ship with dofscroll into your folder"));
    dupBtn_ = mk(T("Duplicate"), T("Copy the selected button mode under a new name"));
    importBtn_ = mk(T("Import file..."), T("Add a button mode file from somewhere else (you see its warnings first)"));
    copyBtn_ = mk(T("Create editable copy"),
                  T("A built-in button mode has no file: create a file with the same id that replaces it"));
    exportBtn_ = mk(T("Export..."), T("Save a copy of the button mode file somewhere else"));
    delBtn_ = mk(T("Delete"), T("Delete the button mode file"));
    folderBtn_ = mk(T("Open button modes folder"), T("Show the folder with the button mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);

    setTabOrder(list_, name_);
    setTabOrder(name_, description_);
    setTabOrder(description_, kind_);
    setTabOrder(kind_, cycle_);
    setTabOrder(cycle_, textEditorBtn_);
    setTabOrder(textEditorBtn_, newBtn_);
    setTabOrder(newBtn_, exampleBtn_);
    setTabOrder(exampleBtn_, dupBtn_);
    setTabOrder(dupBtn_, importBtn_);
    setTabOrder(importBtn_, copyBtn_);
    setTabOrder(copyBtn_, exportBtn_);
    setTabOrder(exportBtn_, delBtn_);
    setTabOrder(delBtn_, folderBtn_);

    connect(list_, &QListWidget::currentItemChanged, this, [this] { onCurrentChanged(); });
    connect(name_, &QLineEdit::textEdited, this, [this] { markDirty(true); });
    connect(description_, &QLineEdit::textEdited, this, [this] { markDirty(true); });
    connect(name_, &QLineEdit::editingFinished, this, [this] { flush(); });  // focus out: now
    connect(description_, &QLineEdit::editingFinished, this, [this] { flush(); });
    connect(kind_, &QComboBox::activated, this, [this](int idx) {
        const std::vector<std::string> ids = spn::ui::bindingModeIds(spn::loadModes(ctx_.modesDir, nullptr));
        binding_ = spn::ui::withKind(binding_, BindingKind(kind_->itemData(idx).toInt()), ids);
        rebuildActionArg();
        markDirty(false);
    });
    connect(cycle_, &CycleEditorWidget::edited, this, [this] { markDirty(false); });
    connect(textEditorBtn_, &QPushButton::clicked, this, [this] { onOpenInEditor(); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { onNew(); });
    connect(exampleBtn_, &QPushButton::clicked, this, [this] { onNewFromExample(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this] { onDuplicate(); });
    connect(importBtn_, &QPushButton::clicked, this, [this] { onImport(); });
    connect(copyBtn_, &QPushButton::clicked, this, [this] { onEditableCopy(); });
    connect(exportBtn_, &QPushButton::clicked, this, [this] { onExport(); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { onDelete(); });
    connect(folderBtn_, &QPushButton::clicked, this, [this] { onOpenFolder(); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ButtonModesPage::refreshIfChanged);

    // The writer: a complete form only; its status goes to the footer of the window.
    saver_ = std::make_unique<Autosaver>([this] { return dirty_ && !shownId_.isEmpty(); }, [this] { return writeForm(); },
                                         ctx_.strings ? *ctx_.strings : ownStrings_, this);
    connect(saver_.get(), &Autosaver::statusChanged, this, [this](const QString &text, bool error) { ctx_.report(text, error); });

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

void ButtonModesPage::onCurrentChanged() {
    const QString target = currentId();
    if (dirty_ && target != shownId_) {
        flush();  // the button mode that was edited is written first; the write repopulates the list
        selectRow(target);
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
    // The catalog is shared: a copy made elsewhere (the profile builder) already refreshed it, so the list is also compared.
    if (!catalog_.refreshIfChanged() && list_->count() == int(catalog_.buttonModes().size())) return;
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
            it->setData(kRoleOrigin, qs8(spn::ui::buttonModeOriginText(b.builtin, b.overridesBuiltin, QtStrings())));
            it->setData(kRoleDescription, b.description.empty() ? qs8(spn::ui::noDescriptionText(QtStrings())) : qs(b.description));
            if (!b.warnings.empty()) it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
            QString tip = b.description.empty() ? qs8(spn::ui::noDescriptionText(QtStrings())) : qs(b.description);
            if (!b.cycle.empty()) tip += QLatin1Char('\n') + qs8(spn::ui::CycleEditorModel::cyclesText(b.cycle, QtStrings()));
            spn::ui::qt::setPlainToolTip(it, tip);
            list_->addItem(it);
            if (id == selectId) select = n;
            ++n;
        }
        if (n) list_->setCurrentRow(select);
    }
    if ((dirty_ || keepForm_) && current() && qs(current()->id) == selectId) {
        // keep the edited form (a write must not reset what is being typed), only refresh the warnings
        updateWarnings();
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
    cycle_->setState(spn::loadModes(ctx_.modesDir, nullptr), e ? e->cycle : std::vector<std::string>(), e && !e->builtin);
    if (use_) use_->setButtonMode(e ? qs(e->id) : QString(), e ? qs(e->label.empty() ? e->id : e->label) : QString());
    fileLabel_->setText(e ? (e->builtin ? T("Built in (no file)") : qs(e->path)) : QString());
    shownId_ = e ? qs(e->id) : QString();
    shownStamp_ = e ? e->fileStamp : std::string();  // the file as this form read it: save refuses a file changed since
    updateWarnings();
    const bool has = e != nullptr;
    const bool editable = has && !e->builtin;  // a built-in button mode has no file to edit
    for (QWidget *w : {static_cast<QWidget *>(name_), static_cast<QWidget *>(description_), static_cast<QWidget *>(kind_),
                       static_cast<QWidget *>(argHost_), static_cast<QWidget *>(textEditorBtn_)})
        w->setEnabled(editable);
    dupBtn_->setEnabled(has);
    exportBtn_->setEnabled(has);
    copyBtn_->setEnabled(has && e->builtin);
    delBtn_->setEnabled(editable);
    dirty_ = false;
    showProblem(QString());
    loading_ = false;
}

// The warnings of the selected entry and of the folder, below the form.
void ButtonModesPage::updateWarnings() {
    const ButtonModeCatalog::Entry *e = current();
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
}

void ButtonModesPage::rebuildActionArg() {
    delete argWidget_;
    argWidget_ = nullptr;
    auto *lay = argHost_->layout();
    if (binding_.kind == BindingKind::Key) {
        // modifier boxes plus the key: a chord such as ctrl+0 is tapped once per press
        auto chord = std::make_shared<std::string>(binding_.arg);
        argWidget_ = chordPicker(argHost_, this, chord.get(), T("Key"), [this, chord] {
            binding_.arg = *chord;
            markDirty(false);
        });
    } else if (binding_.kind == BindingKind::Mode || binding_.kind == BindingKind::ToggleMode ||
               binding_.kind == BindingKind::HoldMode) {
        auto *mc = new QComboBox(argHost_);
        mc->setAccessibleName(T("Joystick mode"));
        mc->setAccessibleDescription(T("Joystick mode"));
        std::vector<std::string> ids = spn::ui::bindingModeIds(spn::loadModes(ctx_.modesDir, nullptr));
        if (!binding_.arg.empty() && std::find(ids.begin(), ids.end(), binding_.arg) == ids.end()) ids.push_back(binding_.arg);
        for (const std::string &id : ids) mc->addItem(qs(id), qs(id));
        mc->setCurrentIndex(std::max(0, mc->findData(qs(binding_.arg))));
        connect(mc, &QComboBox::activated, this, [this, mc](int) {
            binding_.arg = mc->currentData().toString().toStdString();
            markDirty(false);
        });
        argWidget_ = mc;
    }
    if (argWidget_) lay->addWidget(argWidget_);
    cycle_->setVisible(binding_.kind == BindingKind::ModeNext || binding_.kind == BindingKind::ModePrev);
    refreshActionChip();
}

// An edit: an incomplete action (no key chosen) is said, not written; otherwise the write follows (a typed
// text after a pause, a drop-down at once).
// The chip above the action: the action in a few words (muted and "Unassigned" while it is incomplete).
void ButtonModesPage::refreshActionChip() {
    spn::ui::ButtonChip c = spn::ui::buttonChip(1, composedAction(), std::string(), QtStrings());
    c.text = c.action;  // a button mode has no button number
    actionChip_->setButtons({c}, QString());
}

void ButtonModesPage::markDirty(bool typed) {
    refreshActionChip();
    if (loading_) return;
    dirty_ = true;
    if (composedAction().empty()) {
        showProblem(T("Not saved until an action is complete: choose a key or a joystick mode."));
        return;
    }
    showProblem(QString());
    if (typed) saver_->requestDebounced();
    else saver_->requestImmediate();
}

void ButtonModesPage::showProblem(const QString &text) {
    problem_->setText(mark(spn::ui::StatusKind::Error, text));
    problem_->setVisible(!text.isEmpty());
}

std::string ButtonModesPage::composedAction() const { return spn::ui::composeBinding(binding_); }

void ButtonModesPage::showError(const QString &what, const std::string &detail) {
    spn::ui::qt::plainBox(QMessageBox::Warning, this, T(spn::ui::terms::kButtonModes), what + QStringLiteral("\n\n") + qs(detail));
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
    if (!current() || current()->builtin) return false;  // a built-in button mode has no file to edit
    name_->setText(name);
    description_->setText(description);
    Binding b;
    if (!spn::ui::parseBinding(action.toStdString(), &b)) {
        std::fprintf(stderr, "button modes: cannot parse the binding '%s'\n", qPrintable(action));
        return false;
    }
    binding_ = b;
    int idx = 0;
    for (int i = 0; i < kind_->count(); ++i)
        if (kind_->itemData(i).toInt() == int(b.kind)) idx = i;
    kind_->setCurrentIndex(idx);
    rebuildActionArg();
    loading_ = false;
    markDirty(false);
    return true;
}

bool ButtonModesPage::editCurrent(const QString &name, const QString &description, const QString &action) {
    if (!stageEdit(name, description, action)) return false;
    const std::string id = current()->id;
    flush();
    return !dirty_ && catalog_.find(id) != nullptr;
}

// Writes the form under shownId_: the entry the form belongs to, which is not the selected row while the
// selection is being moved to another entry. A complete form only.
spn::ui::Autosave::Outcome ButtonModesPage::writeForm() {
    const std::string id = shownId_.toStdString();
    if (id.empty() || !catalog_.find(id)) return {false, true, "no button mode is selected"};
    const std::string action = composedAction();
    if (action.empty()) {
        std::fprintf(stderr, "button modes: the form composes no action\n");
        return {false, true, "choose a key or a joystick mode first"};
    }
    // the CYCLE list of the "Cycle through" section (see button-modes.md); only with mode:next / mode:prev,
    // an empty list writes no CYCLE key (all joystick modes)
    const std::vector<std::string> cycle =
        action == "mode:next" || action == "mode:prev" ? cycle_->cycle() : std::vector<std::string>();
    const ButtonModeCatalog::Result r =
        catalog_.save(id, name_->text().toUtf8().toStdString(), description_->text().toUtf8().toStdString(), action, cycle,
                      shownStamp_);
    if (r.changedOnDisk) {
        // The file was changed by something else (a text editor, a copy): it is not overwritten. The form shows it as
        // it is now, so the next edit starts from it.
        std::fprintf(stderr, "button modes: '%s' changed on disk, not overwritten\n", id.c_str());
        dirty_ = false;
        catalog_.refresh();
        populate(qs(id));
        return {false, true, "the file changed on disk; it is shown again and your last edit was not saved"};
    }
    if (!r.ok) {
        std::fprintf(stderr, "button modes: saving '%s' failed: %s\n", action.c_str(), r.error.c_str());
        return {false, true, r.error};
    }
    dirty_ = false;
    keepForm_ = true;  // the list shows the new name; the form stays as it is (the caret does not jump)
    populate(qs(id));
    keepForm_ = false;
    shownStamp_ = r.stamp;
    return {true, true, std::string()};
}

void ButtonModesPage::onNew() {
    bool ok = false;
    const QString name = askName(T("New button mode"), T("Name of the new button mode:"), QString(), &ok);
    if (!ok) return;
    flush();
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
    flush();
    const ButtonModeCatalog::Result r = catalog_.duplicate(id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the button mode."), r.error);
    dirty_ = false;
    populate(qs(r.id));
}

void ButtonModesPage::onEditableCopy() {
    const ButtonModeCatalog::Entry *e = current();
    if (!e || !e->builtin) return;
    const std::string id = e->id;  // copy: the timer may rebuild the catalog while the dialog runs
    const QMessageBox::StandardButton a = spn::ui::qt::plainBox(QMessageBox::Question, 
        this, T("Create editable copy"),
        T("The built-in button mode \"%1\" is not a file. Create an editable file with the same id? It replaces the "
          "built-in button mode until you delete the file. To keep the original, use Duplicate instead.")
            .arg(qs(id)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (a != QMessageBox::Yes) return;
    createEditableCopy(qs(id));
}

bool ButtonModesPage::createEditableCopy(const QString &id) {
    flush();
    const ButtonModeCatalog::Result r = catalog_.makeEditableCopy(id.toStdString());
    if (!r.ok) {
        showError(T("Could not create the file."), r.error);
        return false;
    }
    dirty_ = false;
    populate(id);
    return true;
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
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(qs(ButtonModeCatalog::importSafetyNote(QtStrings())));
    QPushButton *yes = box.addButton(T("Import"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    flush();
    const ButtonModeCatalog::Result r = catalog_.importInspected(chk);  // what the user saw, not a second read
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
    box.setTextFormat(Qt::PlainText);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    dirty_ = false;  // the button mode is deleted: edits of it are moot
    const ButtonModeCatalog::Result r = catalog_.remove(id);
    if (!r.ok) return showError(T("Could not delete the button mode."), r.error);
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

// "New from example...": the examples that ship with dofscroll and are not in the folder yet.
void ButtonModesPage::onNewFromExample() {
    const std::vector<spn::ui::Example> ex = spn::ui::availableButtonModeExamples(catalog_, spn::examplesDir());
    if (ex.empty()) {
        spn::ui::qt::plainBox(QMessageBox::Information, this, T("New from example"),
                                 T("All examples are already in your button modes folder, or none ships with this installation."));
        return;
    }
    QStringList items;
    for (const spn::ui::Example &e : ex)
        items << qs(e.label) + QStringLiteral("  -  ") + qs(spn::ui::exampleOriginText(QtStrings())) +
                     (e.description.empty() ? QString() : QStringLiteral("  -  ") + qs(e.description));
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, T("New from example"), T("Copy this example into your button modes folder:"),
                                               items, 0, false, &ok);
    if (!ok) return;
    const int idx = items.indexOf(pick);
    if (idx < 0) return;
    if (!installExample(qs(ex[size_t(idx)].id))) showError(T("Could not add the example."), lastError_.toStdString());
}

bool ButtonModesPage::installExample(const QString &id) {
    lastError_.clear();
    for (const spn::ui::Example &e : spn::ui::availableButtonModeExamples(catalog_, spn::examplesDir())) {
        if (qs(e.id) != id) continue;
        flush();
        const spn::ui::ExampleResult r = spn::ui::installButtonModeExample(catalog_, e);
        if (!r.ok) {
            lastError_ = qs(r.error);
            return false;
        }
        dirty_ = false;
        populate(qs(r.id));
        return true;
    }
    lastError_ = T("This example is not offered.");
    return false;
}

QStringList ButtonModesPage::exampleIds() const {
    QStringList out;
    for (const spn::ui::Example &e : spn::ui::availableButtonModeExamples(catalog_, spn::examplesDir())) out << qs(e.id);
    return out;
}

QString ButtonModesPage::originOf(const QString &id) const {
    const ButtonModeCatalog::Entry *e = catalog_.find(id.toStdString());
    return e ? qs8(spn::ui::buttonModeOriginText(e->builtin, e->overridesBuiltin, QtStrings())) : QString();
}

bool ButtonModesPage::exportButtonModeTo(const QString &id, const QString &dest, bool overwrite) {
    flush();  // the copy holds the last change
    const ButtonModeCatalog::Result r = catalog_.exportTo(id.toStdString(), dest.toStdString(), overwrite);
    lastError_ = qs(r.error);
    return r.ok;
}

void ButtonModesPage::onExport() {
    const ButtonModeCatalog::Entry *e = current();
    if (!e) return;
    const QString id = qs(e->id);
    // The dialog asks before replacing; a file that exists then is replaced atomically by the export itself, so a failed
    // export leaves the old file as it was (it is never removed first).
    const QString file = QFileDialog::getSaveFileName(this, T("Export button mode file"), QDir::homePath() + QLatin1Char('/') + id + QStringLiteral(".conf"),
                                                      T("Button mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    if (!exportButtonModeTo(id, file, QFileInfo::exists(file))) showError(T("Could not export the button mode."), lastError_.toStdString());
}
