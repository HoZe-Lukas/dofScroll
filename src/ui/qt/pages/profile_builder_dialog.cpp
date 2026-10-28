// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "profile_builder_dialog.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

#include "pages/add_app_dialog.hpp"
#include "pages/add_device_dialog.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui/model/profile_tree_model.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
QLabel *wordsLabel(QWidget *parent, const QString &name = QString()) {
    auto *l = new QLabel(parent);
    if (!name.isEmpty()) l->setObjectName(name);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

// A field with its visible label above it; the label is the control's buddy, so a screen reader announces the purpose
// of a combo box (alone it is announced by its current text).
QWidget *labelled(QWidget *parent, const QString &text, QWidget *control) {
    auto *w = new QWidget(parent);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *lab = new QLabel(text, w);
    lab->setTextFormat(Qt::PlainText);
    lab->setBuddy(control);
    // a combo box is announced by its current text: the purpose goes in the description as well
    if (qobject_cast<QComboBox *>(control)) {
        control->setAccessibleName(text);
        control->setAccessibleDescription(text);
    }
    control->setParent(w);
    v->addWidget(lab);
    v->addWidget(control);
    return w;
}

// A combo box that does not force the width of the dialog by its longest item: it shows what fits; the full text is in
// the tooltip and in the list.
void shrinkable(QComboBox *c, int chars) {
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(chars);
    c->view()->setTextElideMode(Qt::ElideMiddle);
    QObject::connect(c, &QComboBox::currentIndexChanged, c, [c] { c->setToolTip(c->currentText()); });
    c->setToolTip(c->currentText());
}
}  // namespace

ProfileBuilderDialog::ProfileBuilderDialog(const UiContext &ctx, const ProfilesEnv &env, QWidget *parent)
    : QDialog(parent), ctx_(ctx), env_(env) {
    setObjectName(QStringLiteral("profileBuilder"));
    setWindowTitle(T("New profile"));
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    SettingsModel &m = *ctx_.settings;
    motionSources_ = motionSources(m, ProfileSet::npos);
    buttonSources_ = buttonSources(m, ProfileSet::npos, ctx_.buttonModes ? &ctx_.buttonModes->set() : nullptr);

    auto *outer = new QVBoxLayout(this);
    auto *intro = wordsLabel(this);
    intro->setText(T("Build a profile from parts that already exist. Afterwards it is a normal profile."));
    intro->setToolTip(T("Take a joystick mode, button modes and single motions or buttons of other profiles. Change the profile on its Motions and buttons tab whenever you like."));
    outer->addWidget(intro);
    auto *area = new QScrollArea(this);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *l = new QVBoxLayout(content);
    area->setWidget(content);
    outer->addWidget(area, 1);

    // 1. kind and name
    auto *who = new QGroupBox(T("What the profile is for"), content);
    auto *wl = new QVBoxLayout(who);
    auto *kinds = new QHBoxLayout;
    app_ = new QRadioButton(T("A program"), who);
    app_->setObjectName(QStringLiteral("builderKindApp"));
    device_ = new QRadioButton(T("A device"), who);
    device_->setObjectName(QStringLiteral("builderKindDevice"));
    app_->setChecked(true);
    kinds->addWidget(app_);
    kinds->addWidget(device_);
    kinds->addStretch(1);
    wl->addLayout(kinds);
    nameLabel_ = new QLabel(who);
    nameLabel_->setObjectName(QStringLiteral("builderNameLabel"));
    nameLabel_->setTextFormat(Qt::PlainText);
    wl->addWidget(nameLabel_);
    auto *nameRow = new QHBoxLayout;
    name_ = new QLineEdit(who);
    name_->setObjectName(QStringLiteral("builderName"));
    nameLabel_->setBuddy(name_);
    choose_ = new QPushButton(T("Choose ..."), who);
    choose_->setObjectName(QStringLiteral("builderChoose"));
    choose_->setToolTip(T("Pick a running program or a connected device"));
    nameRow->addWidget(name_, 1);
    nameRow->addWidget(choose_);
    wl->addLayout(nameRow);
    nameHelp_ = wordsLabel(who, QStringLiteral("builderNameHelp"));
    wl->addWidget(nameHelp_);
    l->addWidget(who);

    // 2. base
    auto *baseBox = new QGroupBox(T("Start from"), content);
    auto *bl = new QVBoxLayout(baseBox);
    base_ = new QComboBox(baseBox);
    base_->setObjectName(QStringLiteral("builderBase"));
    base_->addItem(T("Nothing: an empty profile that inherits everything"), -1);
    for (const SettingsModel::SectionInfo &s : m.sections())
        base_->addItem(qs8(sourceLabel(s.index == 0 ? std::string("Global") : (s.kind == Section::DEVICE ? "Device profile: " : "Program profile: ") + s.pattern, strings)), int(s.index));
    shrinkable(base_, 12);
    bl->addWidget(labelled(baseBox, T("Copy the settings of"), base_));
    auto *baseHelp = wordsLabel(baseBox);
    baseHelp->setText(T("A copy of the settings that profile holds itself (its joystick mode, bindings, tuning). It does not follow the profile afterwards."));
    bl->addWidget(baseHelp);
    l->addWidget(baseBox);

    // 3. joystick mode
    auto *modeBox = new QGroupBox(T("Joystick mode"), content);
    auto *ml = new QVBoxLayout(modeBox);
    mode_ = new QComboBox(modeBox);
    mode_->setObjectName(QStringLiteral("builderMode"));
    mode_->addItem(T("None of its own: the profile follows the joystick mode of Global"));
    modeIds_.push_back(std::string());
    {
        std::vector<NamedChoice> choices;
        std::vector<std::string> ids;
        for (const SettingsModel::ModeOption &o : m.modeOptions(0)) {
            if (o.id.empty() || !o.known) continue;
            choices.push_back({o.label, o.id});
            ids.push_back(o.id);
        }
        const std::vector<std::string> names = plainChoiceNames(choices);
        for (size_t i = 0; i < ids.size(); ++i) {
            mode_->addItem(qs8(names[i]), qs8(ids[i]));
            modeIds_.push_back(ids[i]);
        }
    }
    shrinkable(mode_, 12);
    modeCopy_ = new QCheckBox(T("Make my own copy of this joystick mode"), modeBox);
    modeCopy_->setObjectName(QStringLiteral("builderModeCopy"));
    modeCopy_->setEnabled(false);
    modeWords_ = wordsLabel(modeBox, QStringLiteral("builderModeWords"));
    ml->addWidget(labelled(modeBox, T("Joystick mode of the profile"), mode_));
    ml->addWidget(modeCopy_);
    ml->addWidget(modeWords_);
    l->addWidget(modeBox);

    // 4. button modes per button
    auto *btnBox = new QGroupBox(T("Button modes"), content);
    auto *bbl = new QVBoxLayout(btnBox);
    auto *btnHelp = wordsLabel(btnBox, QStringLiteral("builderButtonHelp"));
    const bool haveButtonModes = ctx_.buttonModes && !ctx_.buttonModes->set().modes.empty();
    btnHelp->setText(haveButtonModes ? T("Give single buttons a button mode. By default the profile follows the button mode by name; tick the copy to give it its own.")
                                     : T("There are no button modes yet. Make one on the Button modes page first."));
    bbl->addWidget(btnHelp);
    buttonRows_ = new QVBoxLayout;
    bbl->addLayout(buttonRows_);
    addButton_ = new QPushButton(T("Add a button mode ..."), btnBox);
    addButton_->setObjectName(QStringLiteral("builderAddButton"));
    addButton_->setEnabled(haveButtonModes);
    bbl->addWidget(addButton_, 0, Qt::AlignLeft);
    l->addWidget(btnBox);

    // 5. single motions and buttons of other profiles
    auto *takeBox = new QGroupBox(T("Take over from other profiles"), content);
    auto *tl = new QVBoxLayout(takeBox);
    auto *takeHelp = wordsLabel(takeBox);
    takeHelp->setText(T("Copy single motions or buttons from another profile or a joystick mode, whatever they are set to there. A copy: later changes in the source do not follow. The lift is not offered yet."));
    tl->addWidget(takeHelp);
    motionRows_ = new QVBoxLayout;
    takeButtonRows_ = new QVBoxLayout;
    tl->addLayout(motionRows_);
    tl->addLayout(takeButtonRows_);
    auto *takeBtns = new QHBoxLayout;
    addMotion_ = new QPushButton(T("Add a motion ..."), takeBox);
    addMotion_->setObjectName(QStringLiteral("builderAddMotion"));
    addTakeButton_ = new QPushButton(T("Add a button ..."), takeBox);
    addTakeButton_->setObjectName(QStringLiteral("builderAddTakeButton"));
    takeBtns->addWidget(addMotion_);
    takeBtns->addWidget(addTakeButton_);
    takeBtns->addStretch(1);
    tl->addLayout(takeBtns);
    l->addWidget(takeBox);
    l->addStretch(1);

    summary_ = wordsLabel(this, QStringLiteral("builderSummary"));
    outer->addWidget(summary_);
    problem_ = wordsLabel(this, QStringLiteral("builderProblem"));
    outer->addWidget(problem_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(T("Create profile"));
    ok_->setObjectName(QStringLiteral("builderCreate"));
    cancel_ = buttons->button(QDialogButtonBox::Cancel);
    cancel_->setText(T("Cancel"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProfileBuilderDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(name_, &QLineEdit::textChanged, this, [this] { validate(); });
    connect(app_, &QRadioButton::toggled, this, [this] { validate(); });
    connect(base_, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        modeCopy_->setEnabled(mode_->currentIndex() > 0);
        if (mode_->currentIndex() <= 0) modeCopy_->setChecked(false);
        validate();
    });
    connect(modeCopy_, &QCheckBox::toggled, this, [this] { validate(); });
    connect(choose_, &QPushButton::clicked, this, [this] { choosePattern(); });
    connect(addButton_, &QPushButton::clicked, this, [this] { addButtonRow(); });
    connect(addMotion_, &QPushButton::clicked, this, [this] { addMotionRow(); });
    connect(addTakeButton_, &QPushButton::clicked, this, [this] { addTakeButtonRow(); });
    validate();
    updateTabOrder();

    // the size comes from the content and the screen, not from a fixed number
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QSize hint = sizeHint().expandedTo(content->sizeHint() + QSize(40, 200));
    const int wide = fontMetrics().averageCharWidth() * 80;  // room for the rows added later, at any font size
    resize(std::min(std::max(hint.width(), wide), int(avail.width() * 0.92)), std::min(std::max(hint.height(), 480), int(avail.height() * 0.92)));
}

// The Tab order follows the screen: widgets added after the construction (the rows) are put where they are shown.
void ProfileBuilderDialog::updateTabOrder() {
    std::vector<QWidget *> chain{app_, device_, name_, choose_, base_, mode_, modeCopy_};
    for (const ButtonRow &r : buttons_) chain.insert(chain.end(), {r.number, r.mode, r.copy, r.remove});
    chain.push_back(addButton_);
    for (const MotionTakeRow &r : motions_) chain.insert(chain.end(), {r.motion, r.source, r.remove});
    for (const TakeButtonRow &r : takeButtons_) chain.insert(chain.end(), {r.number, r.source, r.remove});
    chain.insert(chain.end(), {addMotion_, addTakeButton_, ok_, cancel_});
    for (size_t i = 1; i < chain.size(); ++i) setTabOrder(chain[i - 1], chain[i]);
}

void ProfileBuilderDialog::choosePattern() {
    const bool isApp = app_->isChecked();
    const std::vector<std::string> configured = ProfileTreeModel::patterns(*ctx_.settings, isApp ? Section::APP : Section::DEVICE);
    if (isApp) {
        AddAppDialog dlg(env_, configured, this);
        if (dlg.exec() == QDialog::Accepted) name_->setText(dlg.matchString());
    } else {
        AddDeviceDialog dlg(env_, configured, this);
        if (dlg.exec() == QDialog::Accepted) name_->setText(dlg.deviceName());
    }
}

// Numbers of the buttons that a row already uses (button modes and taken-over buttons share the numbers).
std::set<int> ProfileBuilderDialog::usedButtons() const {
    std::set<int> used;
    for (const ButtonRow &r : buttons_) used.insert(r.number->value());
    for (const TakeButtonRow &r : takeButtons_) used.insert(r.number->value());
    return used;
}

// After a row is gone: the focus goes to a neighbouring control, not to the top of the dialog.
void ProfileBuilderDialog::focusNeighbour(QWidget *next, QWidget *previous, QWidget *fallback) {
    QWidget *w = next ? next : previous ? previous : fallback;
    if (w) w->setFocus(Qt::OtherFocusReason);
}

void ProfileBuilderDialog::addButtonRow() {
    const ButtonModeSet *bm = ctx_.buttonModes ? &ctx_.buttonModes->set() : nullptr;
    if (!bm || bm->modes.empty()) return;
    ButtonRow r;
    r.box = new QWidget(this);
    auto *v = new QVBoxLayout(r.box);  // two lines: the row must fit a narrow dialog and a large font
    v->setContentsMargins(0, 0, 0, 4);
    auto *h = new QHBoxLayout;
    v->addLayout(h);
    r.number = new QSpinBox(r.box);
    r.number->setObjectName(QStringLiteral("builderButtonNumber"));
    r.number->setRange(1, ctx_.settings->buttonLimit());
    r.number->setValue(nextFreeButton(usedButtons(), ctx_.settings->buttonLimit()));
    r.mode = new QComboBox(r.box);
    r.mode->setObjectName(QStringLiteral("builderButtonMode"));
    {
        std::vector<NamedChoice> choices;
        for (const ButtonMode &b : bm->modes) choices.push_back({b.label, b.id});
        const std::vector<std::string> names = plainChoiceNames(choices);
        for (size_t i = 0; i < bm->modes.size(); ++i) r.mode->addItem(qs8(names[i]), qs8(bm->modes[i].id));
    }
    shrinkable(r.mode, 10);
    r.copy = new QCheckBox(T("Own copy"), r.box);
    r.copy->setObjectName(QStringLiteral("builderButtonCopy"));
    r.copy->setToolTip(T("Make my own copy of this button mode"));
    r.remove = new QPushButton(T("Remove"), r.box);
    h->addWidget(labelled(r.box, T("Button"), r.number));
    h->addStretch(1);
    h->addWidget(r.remove, 0, Qt::AlignBottom);
    v->addWidget(labelled(r.box, T("Button mode"), r.mode));
    v->addWidget(r.copy);
    buttonRows_->addWidget(r.box);
    QWidget *box = r.box;
    QSpinBox *num = r.number;
    QPushButton *rm = r.remove;
    auto name = [rm, num] { rm->setAccessibleName(T("Remove the button mode of button %1").arg(num->value())); };
    name();
    connect(r.number, &QSpinBox::valueChanged, this, name);
    connect(rm, &QPushButton::clicked, this, [this, box] {
        QWidget *next = nullptr, *prev = nullptr;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (buttons_[i].box == box) {
                if (i + 1 < buttons_.size()) next = buttons_[i + 1].number;
                if (i > 0) prev = buttons_[i - 1].number;
                buttons_.erase(buttons_.begin() + long(i));
                break;
            }
        }
        focusNeighbour(next, prev, addButton_);
        box->hide();
        box->deleteLater();
        updateTabOrder();
        validate();
    });
    connect(r.number, &QSpinBox::valueChanged, this, [this] { validate(); });
    connect(r.mode, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    connect(r.copy, &QCheckBox::toggled, this, [this] { validate(); });
    buttons_.push_back(r);
    updateTabOrder();
    r.number->setFocus(Qt::OtherFocusReason);
    validate();
}

void ProfileBuilderDialog::addMotionRow() {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    MotionTakeRow r;
    r.box = new QWidget(this);
    auto *v = new QVBoxLayout(r.box);
    v->setContentsMargins(0, 0, 0, 4);
    auto *h = new QHBoxLayout;
    v->addLayout(h);
    r.motion = new QComboBox(r.box);
    r.motion->setObjectName(QStringLiteral("builderMotion"));
    std::vector<std::string> ids;
    for (const spn::ui::MotionRow &mr : profileAssignment(*ctx_.settings, 0, strings).motions) {
        if (mr.id == "lift") continue;  // not offered yet (the builder says so in its help text)
        r.motion->addItem(qs8(mr.name), qs8(mr.id));
        ids.push_back(mr.id);
    }
    std::set<std::string> used;
    for (const MotionTakeRow &o : motions_) used.insert(o.motion->currentData().toString().toStdString());
    const int pre = r.motion->findData(qs8(nextUnusedId(ids, used)));
    if (pre >= 0) r.motion->setCurrentIndex(pre);
    shrinkable(r.motion, 10);
    r.source = new QComboBox(r.box);
    r.source->setObjectName(QStringLiteral("builderMotionSource"));
    for (const BorrowSource &s : motionSources_) r.source->addItem(qs8(sourceLabel(s.label, strings)));
    shrinkable(r.source, 12);
    r.remove = new QPushButton(T("Remove"), r.box);
    h->addWidget(labelled(r.box, T("Motion"), r.motion), 1);
    h->addWidget(r.remove, 0, Qt::AlignBottom);
    v->addWidget(labelled(r.box, T("Take over from"), r.source));
    motionRows_->addWidget(r.box);
    QWidget *box = r.box;
    QComboBox *mo = r.motion;
    QPushButton *rm = r.remove;
    auto name = [rm, mo] { rm->setAccessibleName(T("Remove the motion %1").arg(mo->currentText())); };
    name();
    connect(r.motion, &QComboBox::currentIndexChanged, this, name);
    connect(rm, &QPushButton::clicked, this, [this, box] {
        QWidget *next = nullptr, *prev = nullptr;
        for (size_t i = 0; i < motions_.size(); ++i) {
            if (motions_[i].box == box) {
                if (i + 1 < motions_.size()) next = motions_[i + 1].motion;
                if (i > 0) prev = motions_[i - 1].motion;
                motions_.erase(motions_.begin() + long(i));
                break;
            }
        }
        focusNeighbour(next, prev, addMotion_);
        box->hide();
        box->deleteLater();
        updateTabOrder();
        validate();
    });
    connect(r.motion, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    connect(r.source, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    motions_.push_back(r);
    updateTabOrder();
    r.motion->setFocus(Qt::OtherFocusReason);
    validate();
}

void ProfileBuilderDialog::addTakeButtonRow() {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    TakeButtonRow r;
    r.box = new QWidget(this);
    auto *v = new QVBoxLayout(r.box);
    v->setContentsMargins(0, 0, 0, 4);
    auto *h = new QHBoxLayout;
    v->addLayout(h);
    r.number = new QSpinBox(r.box);
    r.number->setObjectName(QStringLiteral("builderTakeButtonNumber"));
    r.number->setRange(1, ctx_.settings->buttonLimit());
    r.number->setValue(nextFreeButton(usedButtons(), ctx_.settings->buttonLimit()));
    r.source = new QComboBox(r.box);
    r.source->setObjectName(QStringLiteral("builderTakeButtonSource"));
    for (const BorrowSource &s : buttonSources_) r.source->addItem(qs8(sourceLabel(s.label, strings)));
    shrinkable(r.source, 12);
    r.remove = new QPushButton(T("Remove"), r.box);
    h->addWidget(labelled(r.box, T("Button"), r.number));
    h->addStretch(1);
    h->addWidget(r.remove, 0, Qt::AlignBottom);
    v->addWidget(labelled(r.box, T("Take over from"), r.source));
    takeButtonRows_->addWidget(r.box);
    QWidget *box = r.box;
    QSpinBox *num = r.number;
    QPushButton *rm = r.remove;
    auto name = [rm, num] { rm->setAccessibleName(T("Remove the taken-over button %1").arg(num->value())); };
    name();
    connect(r.number, &QSpinBox::valueChanged, this, name);
    connect(rm, &QPushButton::clicked, this, [this, box] {
        QWidget *next = nullptr, *prev = nullptr;
        for (size_t i = 0; i < takeButtons_.size(); ++i) {
            if (takeButtons_[i].box == box) {
                if (i + 1 < takeButtons_.size()) next = takeButtons_[i + 1].number;
                if (i > 0) prev = takeButtons_[i - 1].number;
                takeButtons_.erase(takeButtons_.begin() + long(i));
                break;
            }
        }
        focusNeighbour(next, prev, addTakeButton_);
        box->hide();
        box->deleteLater();
        updateTabOrder();
        validate();
    });
    connect(r.number, &QSpinBox::valueChanged, this, [this] { validate(); });
    connect(r.source, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    takeButtons_.push_back(r);
    updateTabOrder();
    r.number->setFocus(Qt::OtherFocusReason);
    validate();
}

ProfileBuild ProfileBuilderDialog::spec() const {
    ProfileBuild b;
    b.kind = app_->isChecked() ? Section::APP : Section::DEVICE;
    b.name = name_->text().toUtf8().toStdString();
    const int base = base_->currentData().toInt();
    b.base = base < 0 ? ProfileSet::npos : size_t(base);
    const int mi = mode_->currentIndex();
    if (mi > 0 && size_t(mi) < modeIds_.size()) b.modeId = modeIds_[size_t(mi)];
    b.modeOwnCopy = modeCopy_->isChecked() && !b.modeId.empty();
    for (const ButtonRow &r : buttons_) b.buttons.push_back({r.number->value(), r.mode->currentData().toString().toUtf8().toStdString(), r.copy->isChecked()});
    for (const MotionTakeRow &r : motions_) {
        const int si = r.source->currentIndex();
        if (si >= 0 && size_t(si) < motionSources_.size()) b.motions.push_back({r.motion->currentData().toString().toUtf8().toStdString(), motionSources_[size_t(si)], std::string()});
    }
    for (const TakeButtonRow &r : takeButtons_) {
        const int si = r.source->currentIndex();
        if (si >= 0 && size_t(si) < buttonSources_.size()) b.buttonTakes.push_back({r.number->value(), buttonSources_[size_t(si)]});
    }
    return b;
}

QString ProfileBuilderDialog::problemText() const { return problem_->text(); }

// The sentence under the form and the reasons why "Create profile" is off, before anything is written.
void ProfileBuilderDialog::validate() {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const ProfileBuild b = spec();
    modeWords_->setText(b.modeId.empty() ? T("The profile has no joystick mode of its own: it follows the one of Global.")
                        : b.modeOwnCopy ? T("The profile gets its own copy of the joystick mode, a file in your own folder. Changes to the original do not reach it.")
                                        : T("The profile follows this joystick mode by name: when the joystick mode is changed, the profile changes with it."));
    // what the name field is: the label and the plain explanation follow the kind
    nameLabel_->setText(b.kind == Section::APP ? T("Name of the program") : T("Name of the device"));
    nameHelp_->setText(b.kind == Section::APP ? T("The profile is used for the program with this name. Choose ... lists the programs that are running now.")
                                              : T("The profile is used for the device with this name. Choose ... lists the devices that are connected now."));
    name_->setPlaceholderText(b.kind == Section::APP ? T("For example firefox") : T("For example SpaceMouse Pro"));
    std::string norm, err;
    QString problem;
    if (b.name.empty() || !validateProfilePattern(b.name, &norm, &err)) {
        if (!b.name.empty()) problem = T(err.c_str());
    } else if (profileNameTaken(*ctx_.settings, b.kind, norm)) {
        problem = T("A profile with this name already exists.");
    }
    std::set<int> seen;
    for (const ProfileBuild::ButtonChoice &c : b.buttons)
        if (!seen.insert(c.number).second) problem = T("A button is chosen twice.");
    for (const ProfileBuild::ButtonTake &t : b.buttonTakes)
        if (!seen.insert(t.number).second) problem = T("A button is chosen twice.");
    std::set<std::string> motionsSeen;
    for (const ProfileBuild::MotionTake &t : b.motions)
        if (!motionsSeen.insert(t.motion).second) problem = T("A motion is chosen twice.");
    // an empty name is not an error yet, but "Create profile" is off: say what is missing
    problem_->setText(!problem.isEmpty() ? mark(StatusKind::Error, problem) : b.name.empty() ? T("Enter the name of the program or device") : buildError_);
    if (!problem.isEmpty()) buildError_.clear();
    ok_->setEnabled(!b.name.empty() && problem.isEmpty());

    if (b.name.empty()) {
        summary_->clear();  // nothing to summarise before there is a name
        return;
    }
    QStringList parts;
    parts << (b.kind == Section::APP ? T("Program profile") : T("Device profile")) + QStringLiteral(": ") + qs8(norm.empty() ? b.name : norm);
    parts << (b.base == ProfileSet::npos ? T("starts empty") : T("starts as a copy of %1").arg(qs8(sourceLabel(b.base == 0 ? std::string("Global") : (ctx_.settings->sections()[b.base].kind == Section::DEVICE ? "Device profile: " : "Program profile: ") + ctx_.settings->sections()[b.base].pattern, strings))));
    if (!b.modeId.empty()) parts << (b.modeOwnCopy ? T("own copy of the joystick mode %1") : T("follows the joystick mode %1")).arg(qs8(b.modeId));
    if (!b.buttons.empty()) parts << T("%1 button mode(s)").arg(b.buttons.size());
    if (!b.motions.empty() || !b.buttonTakes.empty()) parts << T("%1 motion(s) and %2 button(s) taken over").arg(b.motions.size()).arg(b.buttonTakes.size());
    summary_->setText(parts.join(QStringLiteral("; ")) + QLatin1Char('.'));
}

void ProfileBuilderDialog::accept() {
    ProfileBuild b = spec();
    ProfileBuildResult r = buildProfile(*ctx_.settings, b, ctx_.modes, ctx_.buttonModes);
    if (!r.ok) {
        buildError_ = mark(StatusKind::Error, T(r.error.c_str()));
        problem_->setText(buildError_);
        return;
    }
    created_ = r;
    ctx_.saveSoon();
    QDialog::accept();
}
