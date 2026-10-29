// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/new_function_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "chord_field.hpp"
#include "macro_editor.hpp"
#include "plain_text.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
void setupCombo(QComboBox *c, const QString &name, const QString &description) {
    c->setAccessibleName(name);
    c->setAccessibleDescription(description);  // a combo box reports its current text as its name: the purpose is the description
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);  // long entries are elided, never widen the dialog
    c->setMinimumContentsLength(12);
    c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    c->setMinimumWidth(120);
    c->setMinimumHeight(24);
}
}  // namespace

QLabel *NewFunctionDialog::label(const QString &text, const char *objectName) {
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (objectName) l->setObjectName(QString::fromLatin1(objectName));
    return l;
}

NewFunctionDialog::NewFunctionDialog(const NewFunctionEnv &env, QWidget *parent) : QDialog(parent), env_(env), draft_(env.initial) {
    static const IdentityStrings fallbackStrings;
    if (!env_.strings) env_.strings = &fallbackStrings;
    setObjectName(QStringLiteral("newFunctionDialog"));
    setWindowTitle(env.editing ? T("Function of %1").arg(env.targetName) : T("New function for %1").arg(env.targetName));
    keyMouse_ = env.target.isMotion() && draft_.kind == FunctionKind::Key && (ChannelForm::isMouseWord(draft_.neg) || ChannelForm::isMouseWord(draft_.pos));
    build();
    syncWidgets();
    resize(520, 680);
}

void NewFunctionDialog::build() {
    const StringProvider &strings = *env_.strings;
    const bool motion = env_.target.isMotion();
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("newFunctionScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // the content wraps; the self-test checks that it fits
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *v = new QVBoxLayout(content);
    outer->addWidget(scroll, 1);

    v->addWidget(label(T("This sets what %1 does in %2. Every change is applied as soon as all fields are valid; there is no Save button. A field that is not valid is explained next to it and nothing is written.").arg(env_.targetName, env_.scopeName), "newFunctionIntro"));

    // ---- the kind ----
    auto *kindLabel = label(T("Kind of function:"));
    v->addWidget(kindLabel);
    kind_ = new QComboBox(content);
    kind_->setObjectName(QStringLiteral("newFunctionKind"));
    setupCombo(kind_, T("Kind of function"), T("What %1 should do").arg(env_.targetName));
    kindLabel->setBuddy(kind_);
    QStringList unavailable;
    for (const FunctionKindInfo &k : functionKinds(env_.target, strings)) {
        kind_->addItem(qs8(k.label), int(k.kind));
        pageKinds_.push_back(k.kind);
        auto *model = qobject_cast<QStandardItemModel *>(kind_->model());
        if (!k.available && model && model->item(kind_->count() - 1)) {
            model->item(kind_->count() - 1)->setEnabled(false);
            model->item(kind_->count() - 1)->setToolTip(qs8(k.reason));
            unavailable << qs8(k.label) + QStringLiteral(": ") + qs8(k.reason);
        }
    }
    v->addWidget(kind_);
    kindNotes_ = label(unavailable.isEmpty() ? QString() : T("Not available here: ") + unavailable.join(QLatin1Char(' ')), "newFunctionKindNotes");
    kindNotes_->setVisible(!unavailable.isEmpty());
    v->addWidget(kindNotes_);

    // ---- the fields of the kind ----
    pages_ = new QStackedWidget(content);
    pages_->setObjectName(QStringLiteral("newFunctionPages"));
    for (FunctionKind k : pageKinds_) {
        QWidget *p = nullptr;
        switch (k) {
            case FunctionKind::Key: p = buildKeyPage(); break;
            case FunctionKind::Click: p = buildClickPage(); break;
            case FunctionKind::HoldKey: p = buildHoldPage(); break;
            case FunctionKind::Wheel: p = buildWheelPage(); break;
            case FunctionKind::Pointer: p = buildPointerPage(); break;
            case FunctionKind::Macro: p = buildMacroPage(); break;
        }
        pages_->addWidget(p);
    }
    v->addWidget(pages_);
    actionNote_ = label(QString(), "newFunctionActionNote");
    actionNote_->hide();
    v->addWidget(actionNote_);

    // ---- analog or digital ----
    analog_ = new QCheckBox(T("Analog"), content);  // short: a check box does not wrap, the words below say what it does
    analog_->setObjectName(QStringLiteral("newFunctionAnalog"));
    analog_->setProperty("look", QStringLiteral("box"));
    analog_->setAccessibleName(T("Analog (the key repeats faster the further you push)"));
    analog_->setMinimumHeight(24);
    v->addWidget(analog_);
    analogReason_ = label(QString(), "newFunctionAnalogReason");
    v->addWidget(analogReason_);
    modeWords_ = label(QString(), "newFunctionModeWords");
    v->addWidget(modeWords_);

    // ---- rate and trigger ----
    fieldsHost_ = new QWidget(content);
    auto *fv = new QVBoxLayout(fieldsHost_);
    fv->setContentsMargins(0, 0, 0, 0);
    ratesHost_ = new QWidget(fieldsHost_);
    auto *rv = new QVBoxLayout(ratesHost_);
    rv->setContentsMargins(0, 0, 0, 0);
    rateMin_ = addField(ratesHost_, T("Lowest rate (presses a second)"), "newFunctionRateMin", motion ? keyEngineKey(env_.target.motionId, KeyField::RateMin) : std::string(), &rateMinNote_);
    rateMax_ = addField(ratesHost_, T("Highest rate (presses a second)"), "newFunctionRateMax", motion ? keyEngineKey(env_.target.motionId, KeyField::RateMax) : std::string(), &rateMaxNote_);
    fv->addWidget(ratesHost_);
    triggerHost_ = new QWidget(fieldsHost_);
    auto *tv = new QVBoxLayout(triggerHost_);
    tv->setContentsMargins(0, 0, 0, 0);
    trigger_ = addField(triggerHost_, T("Trigger level"), "newFunctionTrigger", motion ? keyEngineKey(env_.target.motionId, KeyField::Trigger) : std::string(), &triggerNote_);
    triggerLabel_ = triggerHost_->findChild<QLabel *>(QStringLiteral("newFunctionTriggerLabel"));
    fv->addWidget(triggerHost_);
    defaultsWords_ = label(QString(), "newFunctionDefaultsWords");
    fv->addWidget(defaultsWords_);
    v->addWidget(fieldsHost_);

    // ---- the result: the action text, what it does, and whether it is assigned ----
    v->addWidget(label(T("Action text:")));
    preview_ = label(QString(), "newFunctionPreview");
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_->setAccessibleName(T("Action text"));
    preview_->setAccessibleDescription(T("The text of the function as it is stored in the configuration file"));
    v->addWidget(preview_);
    summary_ = label(QString(), "newFunctionSummary");
    v->addWidget(summary_);
    status_ = label(QString(), "newFunctionStatus");
    status_->setAccessibleName(T("State of the function"));
    v->addWidget(status_);
    v->addStretch(1);

    auto *close = new QPushButton(T("Close"), this);
    close->setObjectName(QStringLiteral("newFunctionClose"));
    close->setAccessibleDescription(T("Close the window. The function stays as it is."));
    close->setDefault(false);
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(close);
    outer->addLayout(row);

    connect(kind_, &QComboBox::activated, this, [this](int idx) {
        if (quiet_) return;
        changeKind(&draft_, env_.target, FunctionKind(kind_->itemData(idx).toInt()));
        syncWidgets();
        edited();
    });
    connect(analog_, &QCheckBox::clicked, this, [this](bool on) {
        if (setDraftAnalog(&draft_, on)) {
            syncWidgets();
            edited();
        }
    });
}

QLineEdit *NewFunctionDialog::addField(QWidget *host, const QString &labelText, const char *objectName, const std::string &key, QLabel **note) {
    auto *l = qobject_cast<QVBoxLayout *>(host->layout());
    auto *fieldLabel = label(labelText + QLatin1Char(':'), (std::string(objectName) + "Label").c_str());
    l->addWidget(fieldLabel);
    auto *edit = new QLineEdit(host);
    edit->setObjectName(QString::fromLatin1(objectName));
    edit->setAccessibleName(labelText);
    edit->setAccessibleDescription(env_.targetName + QStringLiteral(": ") + qs8(key));
    edit->setToolTip(qs8(key));
    edit->setMinimumHeight(24);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setMinimumWidth(80);
    fieldLabel->setBuddy(edit);
    l->addWidget(edit);
    *note = label(QString(), "newFunctionFieldNote");
    (*note)->hide();
    l->addWidget(*note);
    connect(edit, &QLineEdit::textEdited, this, [this] {
        if (quiet_) return;
        draft_.rateMin = rateMin_->text().trimmed().toUtf8().toStdString();
        draft_.rateMax = rateMax_->text().trimmed().toUtf8().toStdString();
        draft_.trigger = trigger_->text().trimmed().toUtf8().toStdString();
        edited();
    });
    return edit;
}

QWidget *NewFunctionDialog::buildKeyPage() {
    const bool motion = env_.target.isMotion();
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPageKey"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    for (int side = 0; side < (motion ? 2 : 1); ++side) {
        const QString text = !motion ? T("Key of the button") : side == 0 ? T("Key for the negative direction") : T("Key for the positive direction");
        auto *l = label(text + QLatin1Char(':'));
        v->addWidget(l);
        ChordField::Options opt;
        opt.allowNone = motion;
        opt.mouse = keyMouse_;
        opt.noneText = T("No key");
        auto *f = new ChordField(env_.targetName + QStringLiteral(": ") + text, opt, w);
        f->setObjectName(side == 0 ? QStringLiteral("newFunctionKeyNeg") : QStringLiteral("newFunctionKeyPos"));
        l->setBuddy(f->keyPicker());
        v->addWidget(f);
        (side == 0 ? keyNeg_ : keyPos_) = f;
        connect(f, &ChordField::changed, this, [this] { edited(); });
    }
    if (!motion) v->addWidget(label(T("A key without a modifier is held while you hold the button. A combination, such as Ctrl and C, is pressed once when you press the button."), "newFunctionKeyButtonWords"));
    return w;
}

QWidget *NewFunctionDialog::buildClickPage() {
    const bool motion = env_.target.isMotion();
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPageClick"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    for (int side = 0; side < (motion ? 2 : 1); ++side) {
        const QString text = !motion ? T("Mouse button of the button") : side == 0 ? T("Mouse button for the negative direction") : T("Mouse button for the positive direction");
        auto *l = label(text + QLatin1Char(':'));
        v->addWidget(l);
        auto *c = new QComboBox(w);
        c->setObjectName(side == 0 ? QStringLiteral("newFunctionClickNeg") : QStringLiteral("newFunctionClickPos"));
        c->addItem(T("Left click"), QStringLiteral("click"));
        c->addItem(T("Right click"), QStringLiteral("rightclick"));
        c->addItem(T("Middle click"), QStringLiteral("middleclick"));
        if (motion) c->addItem(T("Nothing"), QStringLiteral("none"));
        setupCombo(c, env_.targetName + QStringLiteral(": ") + text, text);
        l->setBuddy(c);
        v->addWidget(c);
        (side == 0 ? clickNeg_ : clickPos_) = c;
        connect(c, &QComboBox::activated, this, [this](int) {
            if (quiet_) return;
            edited();
        });
    }
    return w;
}

QWidget *NewFunctionDialog::buildHoldPage() {
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPageHold"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    for (int side = 0; side < 2; ++side) {
        const QString text = side == 0 ? T("Key or mouse button held for the negative direction") : T("Key or mouse button held for the positive direction");
        auto *l = label(text + QLatin1Char(':'));
        v->addWidget(l);
        ChordField::Options opt;
        opt.allowNone = true;
        opt.mouse = true;
        opt.noneText = T("Nothing");
        auto *f = new ChordField(env_.targetName + QStringLiteral(": ") + text, opt, w);
        f->setObjectName(side == 0 ? QStringLiteral("newFunctionHoldNeg") : QStringLiteral("newFunctionHoldPos"));
        l->setBuddy(f->keyPicker());
        v->addWidget(f);
        (side == 0 ? holdNeg_ : holdPos_) = f;
        connect(f, &ChordField::changed, this, [this] { edited(); });
    }
    return w;
}

QWidget *NewFunctionDialog::buildWheelPage() {
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPageWheel"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto *l = label(T("What the motion does:"));
    v->addWidget(l);
    wheel_ = new QComboBox(w);
    wheel_->setObjectName(QStringLiteral("newFunctionWheel"));
    wheel_->addItem(T("Scroll"), QStringLiteral("wheel_v"));
    wheel_->addItem(T("Scroll sideways"), QStringLiteral("wheel_h"));
    wheel_->addItem(T("Zoom"), QStringLiteral("zoom"));
    setupCombo(wheel_, T("Scroll or zoom"), T("Scroll, scroll sideways or zoom for %1").arg(env_.targetName));
    l->setBuddy(wheel_);
    v->addWidget(wheel_);
    connect(wheel_, &QComboBox::activated, this, [this](int) {
        if (!quiet_) edited();
    });
    return w;
}

QWidget *NewFunctionDialog::buildPointerPage() {
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPagePointer"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto *l = label(T("Direction of the pointer:"));
    v->addWidget(l);
    pointer_ = new QComboBox(w);
    pointer_->setObjectName(QStringLiteral("newFunctionPointer"));
    pointer_->addItem(T("Move the pointer left and right"), QStringLiteral("pointer_x"));
    pointer_->addItem(T("Move the pointer up and down"), QStringLiteral("pointer_y"));
    setupCombo(pointer_, T("Direction of the pointer"), T("Which way the pointer moves for %1").arg(env_.targetName));
    l->setBuddy(pointer_);
    v->addWidget(pointer_);
    connect(pointer_, &QComboBox::activated, this, [this](int) {
        if (!quiet_) edited();
    });
    return w;
}

QWidget *NewFunctionDialog::buildMacroPage() {
    const bool motion = env_.target.isMotion();
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("newFunctionPageMacro"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto make = [&](const QString &text, const char *objectName) {
        auto *m = new MacroEditor(env_.targetName + QStringLiteral(": ") + text, w);
        m->setObjectName(QString::fromLatin1(objectName));
        connect(m, &MacroEditor::stepsChanged, this, [this] {
            if (!quiet_) edited();
        });
        return m;
    };
    if (motion) {
        // which direction is edited: a drop-down and one macro editor at a time (tabs do not wrap with a wide font)
        auto *dirLabel = label(T("Direction of the macro:"));
        v->addWidget(dirLabel);
        macroDirection_ = new QComboBox(w);
        macroDirection_->setObjectName(QStringLiteral("newFunctionMacroDirection"));
        macroDirection_->addItem(T("Negative direction"));
        macroDirection_->addItem(T("Positive direction"));
        setupCombo(macroDirection_, T("Direction of the macro"), T("One macro for each direction of %1; a direction without steps does nothing").arg(env_.targetName));
        dirLabel->setBuddy(macroDirection_);
        v->addWidget(macroDirection_);
        macroNeg_ = make(T("Macro for the negative direction"), "newFunctionMacroNeg");
        macroPos_ = make(T("Macro for the positive direction"), "newFunctionMacroPos");
        macroPos_->hide();
        v->addWidget(macroNeg_);
        v->addWidget(macroPos_);
        connect(macroDirection_, &QComboBox::activated, this, [this](int idx) {
            macroNeg_->setVisible(idx == 0);
            macroPos_->setVisible(idx == 1);
        });
    } else {
        macroNeg_ = make(T("Macro of the button"), "newFunctionMacroNeg");
        v->addWidget(macroNeg_);
    }
    return w;
}

// The widgets show the draft.
void NewFunctionDialog::syncWidgets() {
    const bool was = quiet_;
    quiet_ = true;
    const bool motion = env_.target.isMotion();
    for (int i = 0; i < kind_->count(); ++i)
        if (FunctionKind(kind_->itemData(i).toInt()) == draft_.kind) {
            kind_->setCurrentIndex(i);
            pages_->setCurrentIndex(i);
        }
    auto pick = [](QComboBox *c, const std::string &value) {
        if (!c) return;
        const int i = c->findData(qs8(value));
        if (i >= 0) c->setCurrentIndex(i);
    };
    switch (draft_.kind) {  // only the page of the kind: the others are filled when their kind is chosen
        case FunctionKind::Key:
            keyNeg_->setValue(draft_.neg);
            if (keyPos_) keyPos_->setValue(draft_.pos);
            break;
        case FunctionKind::Click:
            pick(clickNeg_, draft_.neg);
            pick(clickPos_, draft_.pos);
            break;
        case FunctionKind::HoldKey:
            holdNeg_->setValue(draft_.neg);
            holdPos_->setValue(draft_.pos);
            break;
        case FunctionKind::Wheel: pick(wheel_, draft_.wheel); break;
        case FunctionKind::Pointer: pick(pointer_, draft_.pointer); break;
        case FunctionKind::Macro: break;
    }
    if (draft_.kind == FunctionKind::Macro) {
        macroNeg_->setSteps(draft_.macroNeg);
        if (macroPos_ && motion) macroPos_->setSteps(draft_.macroPos);
    }
    rateMin_->setText(qs8(draft_.rateMin));
    rateMax_->setText(qs8(draft_.rateMax));
    trigger_->setText(qs8(draft_.trigger));
    refreshAnalog();
    refreshFields();
    quiet_ = was;
    std::string text, err;
    preview_->setText(composeFunction(env_.target, draft_, &text, &err) ? qs8(text) : T("(not complete yet)"));
    summary_->setText(qs8(functionSummary(env_.target, draft_, *env_.strings)));
}

void NewFunctionDialog::refreshAnalog() {
    const FunctionAnalog a = functionAnalog(env_.target, draft_, *env_.strings);
    const bool was = quiet_;
    quiet_ = true;
    analog_->setChecked(a.checked);
    analog_->setEnabled(a.allowed);
    quiet_ = was;
    analog_->setAccessibleDescription(env_.targetName + QStringLiteral(": ") + (a.allowed ? T("Switches between a repeating key and one press per push") : qs8(a.reason)));
    analog_->setToolTip(a.allowed ? QString() : qs8(a.reason));
    analogReason_->setText(qs8(a.reason));
    analogReason_->setVisible(!a.reason.empty());
}

void NewFunctionDialog::refreshFields() {
    const FunctionFields f = functionFields(env_.target, draft_);
    ratesHost_->setVisible(f.rates);
    triggerHost_->setVisible(f.trigger);
    fieldsHost_->setVisible(f.rates || f.trigger);
    const FunctionAnalog a = functionAnalog(env_.target, draft_, *env_.strings);
    const bool hold = draft_.kind == FunctionKind::HoldKey;
    const KeyEngineWords words = keyEngineWords(a.checked, env_.effective, env_.target.motionId, *env_.strings, hold);
    modeWords_->setText(env_.target.isMotion() && (draft_.kind == FunctionKind::Key || hold) ? qs8(words.mode) : QString());
    modeWords_->setVisible(!modeWords_->text().isEmpty());
    triggerLabel_->setText(qs8(words.trigger) + QLatin1Char(':'));
    trigger_->setAccessibleName(qs8(words.trigger));
    defaultsWords_->setText(qs8(words.defaults));
    const int km = keyMotionOf(env_.target.motionId);
    if (km >= 0) {
        const KeyTuning &t = env_.effective.key_tuning[km];
        const double trig = (t.set & KeyTuning::F_TRIGGER) ? t.trigger : env_.effective.step_trigger;
        trigger_->setPlaceholderText(QString::number(trig, 'g', 6));
        rateMin_->setPlaceholderText(QString());
        rateMax_->setPlaceholderText(QString());
    }
}

QString NewFunctionDialog::previewText() const { return preview_->text(); }

void NewFunctionDialog::chooseKind(FunctionKind kind) {
    changeKind(&draft_, env_.target, kind);
    syncWidgets();
    edited();
}

// The user changed something: read the widgets of the kind into the draft, then assign when everything is valid.
void NewFunctionDialog::edited() {
    if (quiet_) return;
    const bool motion = env_.target.isMotion();
    bool complete = true;
    switch (draft_.kind) {
        case FunctionKind::Key:
            if (!keyNeg_->value().empty()) draft_.neg = keyNeg_->value();
            else complete = false;
            if (motion) {
                if (!keyPos_->value().empty()) draft_.pos = keyPos_->value();
                else complete = false;
            }
            break;
        case FunctionKind::Click:
            draft_.neg = clickNeg_->currentData().toString().toStdString();
            if (motion) draft_.pos = clickPos_->currentData().toString().toStdString();
            break;
        case FunctionKind::HoldKey:
            if (!holdNeg_->value().empty()) draft_.neg = holdNeg_->value();
            else complete = false;
            if (!holdPos_->value().empty()) draft_.pos = holdPos_->value();
            else complete = false;
            break;
        case FunctionKind::Wheel: draft_.wheel = wheel_->currentData().toString().toStdString(); break;
        case FunctionKind::Pointer: draft_.pointer = pointer_->currentData().toString().toStdString(); break;
        case FunctionKind::Macro:
            draft_.macroNeg = macroNeg_->steps();
            if (motion) draft_.macroPos = macroPos_->steps();
            break;
    }
    refreshAnalog();
    refreshFields();
    std::string text, err;
    preview_->setText(composeFunction(env_.target, draft_, &text, &err) && complete ? qs8(text) : T("(not complete yet)"));
    summary_->setText(qs8(functionSummary(env_.target, draft_, *env_.strings)));
    if (!complete) {
        showResult(false, {{"action", "choose a key for every field first"}});
        return;
    }
    std::vector<FunctionProblem> problems;
    const bool ok = env_.apply ? env_.apply(draft_, &problems) : false;
    showResult(ok, problems);
    if (ok) {
        assigned_ = true;
        ++writes_;
        emit functionAssigned();
    } else {
        assigned_ = false;
    }
}

void NewFunctionDialog::showResult(bool ok, const std::vector<FunctionProblem> &problems) {
    QString action;
    QString rateMin, rateMax, trigger;
    for (const FunctionProblem &p : problems) {
        const QString m = mark(StatusKind::Error, T("Invalid value: ") + qs8(p.message));
        if (p.field == "action") action = m;
        else if (p.field == "rate_min") rateMin = m;
        else if (p.field == "rate_max") rateMax = m;
        else if (p.field == "trigger") trigger = m;
    }
    actionNote_->setText(action);
    actionNote_->setVisible(!action.isEmpty());
    const std::pair<QLabel *, QString> notes[] = {{rateMinNote_, rateMin}, {rateMaxNote_, rateMax}, {triggerNote_, trigger}};
    for (const auto &n : notes) {
        n.first->setText(n.second);
        n.first->setVisible(!n.second.isEmpty());
    }
    if (ok) {
        const QString words = env_.stateWords ? env_.stateWords() : QString();
        status_->setText(mark(StatusKind::Success, T("Assigned.") + (words.isEmpty() ? QString() : QLatin1Char(' ') + words)));
    } else {
        status_->setText(mark(StatusKind::Warning, T("Not assigned yet: nothing is written until every field is valid.")));
    }
}

// ---- opening the dialog for a motion or a button of a profile ----

NewFunctionDialog *openNewFunctionDialog(const UiContext &ctx, size_t section, const FunctionTarget &target, QWidget *parent,
                                         std::function<void()> changed) {
    if (!ctx.settings || section >= ctx.settings->sectionCount()) return nullptr;
    static const IdentityStrings fallback;
    const StringProvider &strings = ctx.strings ? *ctx.strings : fallback;
    SettingsModel *model = ctx.settings;
    NewFunctionEnv env;
    env.target = target;
    env.strings = &strings;
    env.scopeName = section == 0 ? T("Global") : T("this profile");
    env.effective = model->effectiveWithMode(section);
    std::string own;
    if (target.isMotion()) {
        const ProfileAssignment a = profileAssignment(*model, section, strings);
        env.targetName = qs8(target.motionId);
        for (const MotionRow &r : a.motions)
            if (r.id == target.motionId) env.targetName = qs8(r.name);
        const bool has = motion_edit_detail::ownText(*model, section, motion_edit_detail::partKeyOf(target.motionId), &own);
        const std::string tuning = motion_edit_detail::keyTuningMotion(*model, section, target.motionId);
        bool fresh = true;
        if (has) {
            env.initial = draftFromText(target, own, tuning, [model, section](const std::string &key) {
                std::string held;
                motion_edit_detail::ownText(*model, section, key, &held);
                return held;
            }, &fresh);
            env.editing = !fresh;
        }
        if (fresh) {
            env.initial = newDraft(target, FunctionKind::Key);
            env.initial.neg.clear();  // a new function starts without keys: nothing is written until every field is chosen
            env.initial.pos.clear();
        }
    } else {
        env.targetName = T("Button %1").arg(target.button);
        for (const SettingsModel::ButtonRow &r : model->buttonRows(section))
            if (r.number == target.button && r.overridden) own = r.action;
        bool fresh = true;
        if (!own.empty()) {
            env.initial = draftFromText(target, own, std::string(), nullptr, &fresh);
            env.editing = !fresh;
        }
        if (fresh) {
            env.initial = newDraft(target, FunctionKind::Key);
            env.initial.neg.clear();
        }
    }
    UiContext c = ctx;
    env.apply = [c, section, target](const FunctionDraft &d, std::vector<FunctionProblem> *problems) {
        const bool ok = applyFunction(*c.settings, section, target, d, problems);
        if (ok) c.saveSoon();
        return ok;
    };
    const StringProvider *sp = &strings;
    env.stateWords = [model, section, target, sp]() -> QString {
        if (target.isMotion()) return qs8(motionEdit(*model, section, target.motionId, *sp).words);
        return qs8(originBadgeText(buttonOrigin(*model, section, target.button), *sp));
    };
    auto *dlg = new NewFunctionDialog(env, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::WindowModal);
    if (changed) {
        QObject::connect(dlg, &NewFunctionDialog::functionAssigned, dlg, [changed] { changed(); });
        QObject::connect(dlg, &QDialog::finished, dlg, [changed](int) { changed(); });
    }
    dlg->open();
    return dlg;
}
