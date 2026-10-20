// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/mode_editor_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "button_row.hpp"
#include "key_combo.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

struct KindEntry {
    ChannelActionType type;
    const char *label;
};
const KindEntry kKinds[] = {
    {ChannelActionType::Default, "Default (as in the profile)"},
    {ChannelActionType::Off, "Off"},
    {ChannelActionType::WheelV, "Vertical wheel"},
    {ChannelActionType::WheelH, "Horizontal wheel"},
    {ChannelActionType::Zoom, "Zoom (Ctrl + wheel)"},
    {ChannelActionType::Keys, "Keys (a rate of key presses)"},
    {ChannelActionType::Step, "Step (one key press per push)"},
};

// "ctrl+shift+pageup" -> {ctrl, shift} + "pageup"; a lone modifier is the key itself.
void splitChord(const std::string &chord, std::vector<std::string> *mods, std::string *key) {
    mods->clear();
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t p; (p = chord.find('+', start)) != std::string::npos; start = p + 1) parts.push_back(chord.substr(start, p - start));
    parts.push_back(chord.substr(start));
    *key = parts.back();
    parts.pop_back();
    *mods = parts;
}

const char *const kModNames[4] = {"ctrl", "shift", "alt", "super"};

std::string joinChord(const std::vector<std::string> &mods, const std::string &key) {
    std::string out;
    for (const char *m : kModNames)
        if (std::find(mods.begin(), mods.end(), m) != mods.end()) out += std::string(m) + "+";
    return out + key;
}

// Modifier check boxes and the key picker of one side of a keys:/step: action.
QWidget *chordPicker(QWidget *parent, QObject *owner, std::string *chord, const QString &accessible,
                     const std::function<void()> &changed) {
    auto *w = new QWidget(parent);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    std::vector<std::string> mods;
    std::string key;
    splitChord(*chord, &mods, &key);
    for (const char *m : kModNames) {
        auto *cb = new QCheckBox(QString::fromLatin1(m), w);
        cb->setProperty("look", QStringLiteral("box"));
        cb->setChecked(std::find(mods.begin(), mods.end(), m) != mods.end());
        cb->setAccessibleName(accessible + QStringLiteral(" ") + QString::fromLatin1(m));
        h->addWidget(cb);
    }
    auto *kc = makeKeyCombo(QString::fromStdString(key), w);
    kc->setAccessibleName(accessible);
    h->addWidget(kc, 1);
    auto update = [w, chord, kc, changed] {
        std::vector<std::string> sel;
        const auto boxes = w->findChildren<QCheckBox *>();
        for (QCheckBox *cb : boxes)
            if (cb->isChecked()) sel.push_back(cb->text().toStdString());
        if (!kc->currentData().isValid()) return;
        *chord = joinChord(sel, kc->currentData().toString().toStdString());
        changed();
    };
    for (QCheckBox *cb : w->findChildren<QCheckBox *>()) QObject::connect(cb, &QCheckBox::toggled, owner, update);
    QObject::connect(kc, &QComboBox::activated, owner, [update](int) { update(); });
    return w;
}

}  // namespace

ModeEditorWidget::ModeEditorWidget(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx), dir_(ctx.modesDir) {
    auto *root = new QVBoxLayout(this);
    title_ = new QLabel(this);
    root->addWidget(title_);
    // Sub-tabs, each scrolling on its own; the problem list and the buttons below stay visible.
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setAccessibleName(T("Mode sections"));
    root->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && tabs_->widget(i)) lastTab_ = tabs_->widget(i)->property("tabId").toString();
    });
    problems_ = new QLabel(this);
    problems_->setWordWrap(true);
    problems_->setTextFormat(Qt::PlainText);
    problems_->setAccessibleName(T("Problems"));
    root->addWidget(problems_);
    warnings_ = new QPlainTextEdit(this);
    warnings_->setReadOnly(true);
    warnings_->setMaximumHeight(110);
    warnings_->setAccessibleName(T("Warnings of the saved file"));
    warnings_->hide();
    root->addWidget(warnings_);
    auto *row = new QHBoxLayout;
    textBtn_ = new QPushButton(T("Open in text editor"), this);
    closeBtn_ = new QPushButton(T("Close editor"), this);
    row->addWidget(textBtn_);
    row->addStretch(1);
    row->addWidget(closeBtn_);
    root->addLayout(row);
    connect(textBtn_, &QPushButton::clicked, this, [this] {
        flush();  // the text editor shows what is written
        std::string err;
        if (ctx_.runner && !ctx_.runner->openPath(editor_.path(), &err))
            QMessageBox::warning(this, T("Joystick modes"), T("Could not open the file.") + QStringLiteral("\n\n") + qs8(err));
    });
    connect(closeBtn_, &QPushButton::clicked, this, [this] {
        flush();  // nothing is lost: the last valid state is written before the editor goes
        emit closeRequested();
    });
    // The writer: valid changes only; its status goes to the footer of the window.
    saver_ = std::make_unique<Autosaver>(
        [this] { return editor_.isOpen() && editor_.dirty() && editor_.validate().empty(); },
        [this] {
            const ModeEditor::SaveResult r = editor_.save(false);
            if (!r.ok && r.changedOnDisk) {
                const bool overwrite =
                    overwriteAnswer_ ? overwriteAnswer_()
                                     : QMessageBox::question(this, T("Joystick modes"),
                                                             T("The file changed on disk while you were editing. "
                                                               "Overwrite it with your changes?"),
                                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
                                           QMessageBox::Yes;
                if (overwrite) return finishWrite(editor_.save(true));
            }
            return finishWrite(r);
        },
        ctx_.strings ? *ctx_.strings : ownStrings_, this);
    connect(saver_.get(), &Autosaver::statusChanged, this, [this](const QString &text, bool error) { ctx_.report(text, error); });
}

bool ModeEditorWidget::open(const std::string &path, const ModeDef &def) {
    flush();  // the mode that was edited before is written first
    std::string err;
    if (!editor_.open(path, def, &err)) {
        QMessageBox::warning(this, T("Joystick modes"), T("Could not open the file.") + QStringLiteral("\n\n") + qs8(err));
        return false;
    }
    warnings_->hide();
    rebuild();
    return true;
}

int ModeEditorWidget::addChannelRow(QGridLayout *g, int row, const QString &label, ChannelForm *form) {
    auto *name = new QLabel(label, this);
    auto *kind = new QComboBox(this);
    kind->setAccessibleName(label);
    int cur = 0;
    for (size_t i = 0; i < sizeof(kKinds) / sizeof(kKinds[0]); ++i) {
        kind->addItem(T(kKinds[i].label), int(kKinds[i].type));
        if (kKinds[i].type == form->type) cur = int(i);
    }
    kind->setCurrentIndex(cur);
    g->addWidget(name, row, 0);
    g->addWidget(kind, row, 1);
    int used = 1;
    if (form->usesKeys()) {
        // One line per direction below the drop-down, so the form never needs a horizontal scroll bar.
        auto changed = [this] { edited(false); };
        g->addWidget(new QLabel(T("Negative:"), this), row + 1, 0, Qt::AlignRight);
        g->addWidget(chordPicker(this, this, &form->neg, label + QStringLiteral(" ") + T("negative"), changed), row + 1, 1);
        g->addWidget(new QLabel(T("Positive:"), this), row + 2, 0, Qt::AlignRight);
        g->addWidget(chordPicker(this, this, &form->pos, label + QStringLiteral(" ") + T("positive"), changed), row + 2, 1);
        used = 3;
    }
    connect(kind, &QComboBox::activated, this, [this, kind, form](int idx) {
        form->type = ChannelActionType(kind->itemData(idx).toInt());
        if (form->usesKeys() && (form->neg.empty() || form->pos.empty())) {
            form->neg = "up";
            form->pos = "down";
        }
        rebuild();
        edited(false);
    });
    return used;
}

void ModeEditorWidget::rebuild() {
    ModeForm &f = editor_.form();
    title_->setText(QStringLiteral("<b>") + T("Edit joystick mode: %1").arg(qs8(f.id)).toHtmlEscaped() + QStringLiteral("</b>"));
    // One scrolling page per tab: settings (name, description, channels), buttons, overrides.
    int scrollPos = 0;
    if (auto *cur = qobject_cast<QScrollArea *>(tabs_->currentWidget())) scrollPos = cur->verticalScrollBar()->value();
    QSignalBlocker block(tabs_);
    while (tabs_->count() > 0) {
        QWidget *w = tabs_->widget(0);
        tabs_->removeTab(0);
        w->deleteLater();
    }
    auto newTab = [this](const char *id, const QString &label, QWidget **content) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setProperty("tabId", QString::fromLatin1(id));
        *content = new QWidget;
        scroll->setWidget(*content);
        tabs_->addTab(scroll, label);
        return new QVBoxLayout(*content);
    };
    QWidget *content = nullptr, *buttonsPage = nullptr, *overridesPage = nullptr;
    auto *col = newTab("settings", T("Settings"), &content);

    // Name and description
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(qs8(f.name), content);
    name->setAccessibleName(T("Name"));
    name->setPlaceholderText(qs8(f.id));
    auto *desc = new QLineEdit(qs8(f.description), content);
    desc->setAccessibleName(T("Description"));
    form->addRow(T("Name:"), name);
    form->addRow(T("Description:"), desc);
    col->addLayout(form);
    connect(name, &QLineEdit::textEdited, this, [this](const QString &t) {
        editor_.form().name = t.toUtf8().toStdString();
        edited(true);
    });
    connect(desc, &QLineEdit::textEdited, this, [this](const QString &t) {
        editor_.form().description = t.toUtf8().toStdString();
        edited(true);
    });
    connect(name, &QLineEdit::editingFinished, this, [this] { flush(); });  // focus out: now
    connect(desc, &QLineEdit::editingFinished, this, [this] { flush(); });

    // Channels
    auto *chBox = new QGroupBox(T("Channels"), content);
    auto *cg = new QGridLayout(chBox);
    cg->setColumnStretch(1, 1);
    int crow = 0;
    crow += addChannelRow(cg, crow, T("Vertical push"), &f.v);
    crow += addChannelRow(cg, crow, T("Horizontal push"), &f.h);
    crow += addChannelRow(cg, crow, T("Twist"), &f.twist);
    col->addWidget(chBox);

    // Buttons: the shared row
    auto *bcol = newTab("buttons", T("Buttons"), &buttonsPage);
    auto *btBox = new QGroupBox(T("Buttons"), buttonsPage);
    auto *bl = new QVBoxLayout(btBox);
    auto *bg = new QGridLayout;
    bg->setColumnStretch(2, 1);
    bl->addLayout(bg);
    const ButtonModeSet buttonModes = loadButtonModes(ctx_.buttonModesDir, nullptr);
    ButtonRowEnv env;
    env.context = BindingContext::Specific;  // mode bindings are mode-specific: every action is first-class
    env.advancedOpen = true;
    env.buttonModes = &buttonModes;
    env.modeIds = bindingModeIds(loadModes(dir_, nullptr));
    env.strings = ctx_.strings ? ctx_.strings : &ownStrings_;
    env.showInherited = false;
    env.resetText = T("Unbind");
    env.resetTip = T("Remove this binding; the profile's binding for the button applies again");
    int limit = ctx_.settings ? ctx_.settings->buttonLimit() : 8;
    for (const auto &b : f.buttons) limit = std::max(limit, b.first);
    limit = std::min(limit, 32);
    const std::vector<std::string> modeIds = env.modeIds;
    for (int n = 1; n <= limit; ++n) {
        BindingRow row;
        row.number = n;
        const std::string *act = f.button(n);
        row.text = act ? *act : std::string("none");
        row.valid = parseBinding(row.text, &row.binding);
        row.overridden = act != nullptr;
        ButtonRowHandlers h;
        h.choose = [this, n, row, modeIds](const ButtonChoice &c) {
            ModeForm &mf = editor_.form();
            if (c.type == ButtonChoice::Type::Inherit) {
                mf.setButton(n, "none");
            } else {
                Binding b;
                if (c.type == ButtonChoice::Type::ButtonMode) b = Binding{BindingKind::ButtonMode, c.id};
                else b = withKind(row.binding, c.kind, modeIds);
                const std::string text = composeBinding(b);
                if (!text.empty()) mf.setButton(n, text);
            }
            rebuild();
            edited(false);
        };
        h.setArgument = [this, n](const Binding &b) {
            const std::string text = composeBinding(b);
            if (!text.empty()) editor_.form().setButton(n, text);
            rebuild();
            edited(false);
        };
        h.reset = [this, n] {
            editor_.form().setButton(n, "none");
            rebuild();
            edited(false);
        };
        addButtonRow(this, btBox, bg, n - 1, row, env, h);
    }
    bcol->addWidget(btBox);
    bcol->addStretch(1);

    // Overrides
    auto *ocol = newTab("overrides", T("Overrides"), &overridesPage);
    auto *ovBox = new QGroupBox(T("Settings overrides"), overridesPage);
    auto *ol = new QVBoxLayout(ovBox);
    ol->addWidget(new QLabel(T("Values that apply while this mode is active (for example the sensitivity)."), overridesPage));
    auto *og = new QFormLayout;
    for (size_t i = 0; i < f.overrides.size(); ++i) {
        auto *rowW = new QWidget(overridesPage);
        auto *h = new QHBoxLayout(rowW);
        h->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(qs8(f.overrides[i].second), rowW);
        edit->setAccessibleName(qs8(f.overrides[i].first));
        auto *rm = new QToolButton(rowW);
        rm->setText(T("Remove"));
        h->addWidget(edit, 1);
        h->addWidget(rm);
        connect(edit, &QLineEdit::textEdited, this, [this, i](const QString &t) {
            editor_.form().overrides[i].second = t.toUtf8().toStdString();
            edited(true);
        });
        connect(edit, &QLineEdit::editingFinished, this, [this] { flush(); });
        connect(rm, &QToolButton::clicked, this, [this, i] {
            auto &ov = editor_.form().overrides;
            ov.erase(ov.begin() + long(i));
            rebuild();
            edited(false);
        });
        og->addRow(qs8(f.overrides[i].first) + QLatin1Char(':'), rowW);
    }
    ol->addLayout(og);
    const std::vector<std::string> addable = editor_.addableOverrideKeys();
    if (!addable.empty()) {
        auto *ar = new QHBoxLayout;
        auto *combo = new QComboBox(overridesPage);
        combo->setAccessibleName(T("Setting to override"));
        for (const std::string &k : addable) combo->addItem(qs8(k), qs8(k));
        auto *add = new QPushButton(T("Add override"), overridesPage);
        ar->addWidget(combo, 1);
        ar->addWidget(add);
        ol->addLayout(ar);
        connect(add, &QPushButton::clicked, this, [this, combo] {
            const std::string key = combo->currentData().toString().toStdString();
            Settings def;
            std::string value;
            for (const KV &kv : allKeys(def))
                if (kv.first == key) value = kv.second;
            editor_.form().overrides.emplace_back(key, value);
            rebuild();
            edited(false);
        });
    }
    ocol->addWidget(ovBox);
    ocol->addStretch(1);
    col->addStretch(1);

    int idx = 0;
    for (int i = 0; i < tabs_->count(); ++i)
        if (tabs_->widget(i)->property("tabId").toString() == lastTab_) idx = i;
    tabs_->setCurrentIndex(idx);
    block.unblock();
    if (scrollPos > 0)
        if (auto *cur = qobject_cast<QScrollArea *>(tabs_->currentWidget()))
            QTimer::singleShot(0, cur, [cur, scrollPos] { cur->verticalScrollBar()->setValue(scrollPos); });
    showProblems();
}

QString ModeEditorWidget::problemText() const {
    const std::vector<EditProblem> p = editor_.validate();
    QString t;
    for (const EditProblem &x : p) {
        QString where = qs8(x.field);
        if (x.field == "v") where = T("Vertical push");
        else if (x.field == "h") where = T("Horizontal push");
        else if (x.field == "twist") where = T("Twist");
        else if (x.field.rfind("button:", 0) == 0) where = T("Button") + QLatin1Char(' ') + qs8(x.field.substr(7));
        else if (x.field.rfind("override:", 0) == 0) where = qs8(x.field.substr(9));
        t += where + QStringLiteral(": ") + qs8(x.message) + QLatin1Char('\n');
    }
    return t.trimmed();
}

void ModeEditorWidget::showProblems() {
    const QString t = problemText();
    // An invalid form is never written; the text says so, next to the fields that need fixing.
    problems_->setText(t.isEmpty() ? QString() : T("Not saved until this is fixed:") + QLatin1Char('\n') + t);
    problems_->setProperty("error", !t.isEmpty());
    problems_->style()->unpolish(problems_);
    problems_->style()->polish(problems_);
    problems_->setVisible(!t.isEmpty());
}

// An edit changed the form: show what is wrong, or write it (a typed text after a pause).
void ModeEditorWidget::edited(bool typed) {
    showProblems();
    if (!editor_.validate().empty()) return;
    if (typed) saver_->requestDebounced();
    else saver_->requestImmediate();
}

Autosave::Outcome ModeEditorWidget::finishWrite(const ModeEditor::SaveResult &r) {
    if (!r.ok) {
        showProblems();
        std::string why = r.error;
        if (why.empty() && r.changedOnDisk) why = "the file changed on disk";
        if (why.empty() && !r.problems.empty()) why = r.problems.front().field + ": " + r.problems.front().message;
        return Autosave::Outcome{false, true, why};
    }
    QString w;
    for (const std::string &s : r.warnings) w += QStringLiteral(" - ") + qs8(s) + QLatin1Char('\n');
    warnings_->setPlainText(w.isEmpty() ? T("The saved file has no loader warnings.") : T("Warnings:") + QLatin1Char('\n') + w);
    warnings_->show();
    showProblems();
    emit saved(qs8(editor_.form().id));
    return Autosave::Outcome{true, true, std::string()};
}

bool ModeEditorWidget::flush() {
    const bool wrote = saver_->flush();
    return wrote && problemText().isEmpty();  // an invalid form is not written: say so
}

bool ModeEditorWidget::save(bool overwriteChanged) {
    if (!editor_.isOpen()) return false;
    const ModeEditor::SaveResult r = editor_.save(overwriteChanged);
    return finishWrite(r).saved;
}

QStringList ModeEditorWidget::tabIds() const {
    QStringList out;
    for (int i = 0; i < tabs_->count(); ++i) out << tabs_->widget(i)->property("tabId").toString();
    return out;
}

QString ModeEditorWidget::currentTabId() const {
    return tabs_->currentWidget() ? tabs_->currentWidget()->property("tabId").toString() : QString();
}

bool ModeEditorWidget::selectTab(const QString &id) {
    for (int i = 0; i < tabs_->count(); ++i)
        if (tabs_->widget(i)->property("tabId").toString() == id) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    return false;
}
