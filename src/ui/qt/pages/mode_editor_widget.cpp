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
#include "pages/motion_editor.hpp"
#include "ui/model/overview_model.hpp"
#include "ui/model/ui_progress.hpp"
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

}  // namespace

ModeEditorWidget::ModeEditorWidget(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx), dir_(ctx.modesDir) {
    auto *root = new QVBoxLayout(this);
    title_ = new QLabel(this);
    root->addWidget(title_);
    // Sub-tabs, each scrolling on its own; the problem list and the buttons below stay visible.
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setAccessibleName(T("Joystick mode sections"));
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

namespace {
const char *const kPartTuning[][2] = {{"SENS", "Sensitivity"},
                                      {"DEADZONE", "Dead zone"},
                                      {"SIGN", "Direction (sign)"},
                                      {"CURVE", "Curve"},
                                      {"SMOOTH_MS", "Smoothing (ms)"}};

// True for SCROLL_<V|H>_<PUSH|TILT>_* of a channel that this mode splits: the split section shows
// these, the Overrides tab leaves them out.
bool shownInSplit(const ModeForm &f, const std::string &key) {
    const std::string k = utf8::upperAscii(key);
    return (k.rfind("SCROLL_V_PUSH_", 0) == 0 || k.rfind("SCROLL_V_TILT_", 0) == 0) ? f.vSplit.active()
           : (k.rfind("SCROLL_H_PUSH_", 0) == 0 || k.rfind("SCROLL_H_TILT_", 0) == 0) ? f.hSplit.active()
                                                                                       : false;
}
}  // namespace

int ModeEditorWidget::addSplitChannel(QGridLayout *g, int row, const QString &label, const QString &pushLabel,
                                      const QString &tiltLabel, ChannelForm *channel, SplitForm *split, const char *prefix) {
    int used = 0;
    if (!split->active()) {
        used += addChannelRow(g, row, label, channel);
    } else {
        used += addChannelRow(g, row + used, pushLabel, &split->push);
        used += addChannelRow(g, row + used, tiltLabel, &split->tilt);
    }
    auto *sw = new QCheckBox(T("Split push and tilt"), this);
    sw->setAccessibleName(label + QStringLiteral(": ") + T("Split push and tilt"));
    sw->setToolTip(T("Give the push movement (forward/back, sideways) and the tilt movement their own functions"));
    sw->setChecked(split->active());
    g->addWidget(sw, row + used, 1);
    ++used;
    connect(sw, &QCheckBox::clicked, this, [this, split, channel](bool on) {
        split->setSplit(on, *channel);
        rebuild();
        edited(false);
    });
    if (split->active()) {
        const char *partIds[2] = {"PUSH", "TILT"};
        const QString partNames[2] = {pushLabel, tiltLabel};
        for (int p = 0; p < 2; ++p) {
            auto *box = new QGroupBox(T("Tuning: %1").arg(partNames[p]), this);
            auto *fl = new QFormLayout(box);
            for (const auto &t : kPartTuning) {
                const std::string key = std::string("SCROLL_") + prefix + "_" + partIds[p] + "_" + t[0];
                auto *e = new QLineEdit(this);
                const std::string *cur = editor_.form().overrideValue(key);
                e->setText(cur ? qs8(*cur) : QString());
                e->setPlaceholderText(T("As the channel"));
                e->setAccessibleName(partNames[p] + QStringLiteral(": ") + T(t[1]));
                e->setToolTip(qs8(key));
                fl->addRow(T(t[1]) + QLatin1Char(':'), e);
                connect(e, &QLineEdit::textEdited, this, [this, key](const QString &text) {
                    editor_.form().setOverride(key, text.trimmed().toUtf8().toStdString());
                    edited(true);
                });
                connect(e, &QLineEdit::editingFinished, this, [this] { flush(); });
            }
            g->addWidget(box, row + used, 0, 1, 2);
            ++used;
        }
    }
    return used;
}

ButtonRowEnv ModeEditorWidget::buttonRowEnv(const ButtonModeSet &buttonModes) const {
    ButtonRowEnv env;
    env.context = BindingContext::Specific;  // mode bindings are mode-specific: every action is first-class
    env.advancedOpen = true;
    env.buttonModes = &buttonModes;
    env.modeIds = bindingModeIds(loadModes(dir_, nullptr));
    env.strings = ctx_.strings ? ctx_.strings : &ownStrings_;
    env.showInherited = false;
    env.resetText = T("Unbind");
    env.resetTip = T("Remove this binding; the profile's binding for the button applies again");
    return env;
}

// One button row of the shared editor for button n (the Buttons tab and the Motions and buttons tab).
void ModeEditorWidget::addBindingRow(QWidget *parent, QGridLayout *grid, int gridRow, int n, const ButtonRowEnv &env) {
    ModeForm &f = editor_.form();
    BindingRow row;
    row.number = n;
    const std::string *act = f.button(n);
    row.text = act ? *act : std::string("none");
    row.valid = parseBinding(row.text, &row.binding);
    row.overridden = act != nullptr;
    const std::vector<std::string> modeIds = env.modeIds;
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
    addButtonRow(this, parent, grid, gridRow, row, env, h);
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
    QWidget *motionsPage = nullptr, *content = nullptr, *buttonsPage = nullptr, *overridesPage = nullptr;
    auto *mcol = newTab("motions", T("Motions and buttons"), &motionsPage);
    {
        MotionEditorEnv menv;
        menv.form = &f;
        menv.base = ctx_.settings ? ctx_.settings->effective(0) : Settings();
        menv.strings = ctx_.strings ? ctx_.strings : &ownStrings_;
        if (ctx_.status) menv.deviceName = ctx_.status->status().device;
        menv.reportedButtons = ctx_.settings ? reportedOrDefaultButtons(ctx_.settings->buttonLimit()) : kUnknownDeviceButtons;
        menv.changed = [this] {
            rebuild();
            edited(false);
        };
        menv.committed = [this] { edited(false); };
        menv.typed = [this] { edited(true); };
        menv.flush = [this] { flush(); };
        menv.showTab = [this](const QString &id) { selectTab(id); };
        // live parts: detect by moving, try, the guided tour
        menv.runtimeDir = ctx_.runtimeDir;
        menv.daemonRunning = [this] { return ctx_.status && ctx_.status->daemonRunning(); };
        menv.activeMode = [this] { return ctx_.status ? ctx_.status->modeId() : std::string(); };
        if (ctx_.runner)
            menv.tryMode = [this](const std::string &id, std::string *err) { return requestActiveMode(ctx_.runtimeDir, *ctx_.runner, id, err); };
        menv.uiProgressFile = ctx_.configPath.empty() ? std::string() : uiProgressPath(ctx_.configPath);
        if (!motionTour_) {
            motionTour_ = std::make_unique<TourModel>(false);
            // shown the first time the tab opens; not again once it ran to its end or was dismissed
            if (!menv.uiProgressFile.empty() && loadUiProgress(menv.uiProgressFile).motionTourSeen) motionTour_->dismiss();
        }
        menv.tour = motionTour_.get();
        menv.detect = motionDetect_;
        menv.detectChanged = [this](bool on) { motionDetect_ = on; };
        menv.tryingFrom = motionTrying_;
        menv.tryingChanged = [this](const std::string &from) { motionTrying_ = from; };
        menv.addButtonRow = [this](QWidget *parent, QGridLayout *grid, int gridRow, int n) {
            static thread_local ButtonModeSet keep;
            keep = loadButtonModes(ctx_.buttonModesDir, nullptr);
            addBindingRow(parent, grid, gridRow, n, buttonRowEnv(keep));
        };
        motionEditor_ = new MotionEditor(menv, motionSel_, motionsPage);
        connect(motionEditor_, &MotionEditor::selectionChanged, this, [this](const QString &id) { motionSel_ = id; });
        mcol->addWidget(motionEditor_);
        mcol->addStretch(1);
    }
    auto *col = newTab("settings", T("Settings"), &content);

    // Name and description
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(qs8(f.name), content);
    name->setAccessibleName(T("Name"));
    name->setPlaceholderText(qs8(f.id));
    auto *desc = new QLineEdit(qs8(f.description), content);
    desc->setAccessibleName(T("Description"));
    desc->setPlaceholderText(T("No description"));
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
    crow += addSplitChannel(cg, crow, T("Vertical push"), T("Vertical push"), T("Vertical tilt"), &f.v, &f.vSplit, "V");
    crow += addSplitChannel(cg, crow, T("Horizontal push"), T("Horizontal push"), T("Horizontal tilt"), &f.h, &f.hSplit, "H");
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
    const ButtonRowEnv env = buttonRowEnv(buttonModes);
    int limit = ctx_.settings ? ctx_.settings->buttonLimit() : 8;
    for (const auto &b : f.buttons) limit = std::max(limit, b.first);
    limit = std::min(limit, 32);
    for (int n = 1; n <= limit; ++n) addBindingRow(btBox, bg, n - 1, n, env);
    bcol->addWidget(btBox);
    bcol->addStretch(1);

    // Overrides
    auto *ocol = newTab("overrides", T("Overrides"), &overridesPage);
    auto *ovBox = new QGroupBox(T("Settings overrides"), overridesPage);
    auto *ol = new QVBoxLayout(ovBox);
    ol->addWidget(new QLabel(T("Values that apply while this joystick mode is active (for example the sensitivity)."), overridesPage));
    auto *og = new QFormLayout;
    for (size_t i = 0; i < f.overrides.size(); ++i) {
        if (shownInSplit(f, f.overrides[i].first)) continue;
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
        for (const std::string &k : addable)
            if (!shownInSplit(f, k)) combo->addItem(qs8(k), qs8(k));
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
        else if (x.field == "v_push") where = T("Vertical push");
        else if (x.field == "v_tilt") where = T("Vertical tilt");
        else if (x.field == "h_push") where = T("Horizontal push");
        else if (x.field == "h_tilt") where = T("Horizontal tilt");
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
    problems_->setText(t.isEmpty() ? QString()
                                   : mark(spn::ui::StatusKind::Error, T("Not saved until this is fixed:")) + QLatin1Char('\n') + t);
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
