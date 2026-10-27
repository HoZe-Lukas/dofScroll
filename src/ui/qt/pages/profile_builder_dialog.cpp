// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "profile_builder_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

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
}  // namespace

ProfileBuilderDialog::ProfileBuilderDialog(const UiContext &ctx, const ProfilesEnv &env, QWidget *parent)
    : QDialog(parent), ctx_(ctx), env_(env) {
    setObjectName(QStringLiteral("profileBuilder"));
    setWindowTitle(T("New profile"));
    resize(680, 720);
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    SettingsModel &m = *ctx_.settings;
    motionSources_ = motionSources(m, ProfileSet::npos);
    buttonSources_ = buttonSources(m, ProfileSet::npos, ctx_.buttonModes ? &ctx_.buttonModes->set() : nullptr);

    auto *outer = new QVBoxLayout(this);
    auto *intro = wordsLabel(this);
    intro->setText(T("Build a profile from what already exists: a joystick mode, button modes and single motions or buttons of other profiles. Afterwards it is a normal profile; change it on its Motions and buttons tab whenever you like."));
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
    auto *nameRow = new QHBoxLayout;
    name_ = new QLineEdit(who);
    name_->setObjectName(QStringLiteral("builderName"));
    name_->setAccessibleName(T("Match name"));
    name_->setPlaceholderText(T("The name of the program or device the profile applies to"));
    auto *choose = new QPushButton(T("Choose ..."), who);
    choose->setObjectName(QStringLiteral("builderChoose"));
    choose->setToolTip(T("Pick a running program or a connected device"));
    nameRow->addWidget(name_, 1);
    nameRow->addWidget(choose);
    wl->addLayout(nameRow);
    l->addWidget(who);

    // 2. base
    auto *baseBox = new QGroupBox(T("Start from"), content);
    auto *bl = new QVBoxLayout(baseBox);
    base_ = new QComboBox(baseBox);
    base_->setObjectName(QStringLiteral("builderBase"));
    base_->setAccessibleName(T("Start from"));
    base_->addItem(T("Nothing: an empty profile that inherits everything"), -1);
    for (const SettingsModel::SectionInfo &s : m.sections())
        base_->addItem(qs8(sourceLabel(s.index == 0 ? std::string("Global") : (s.kind == Section::DEVICE ? "Device profile: " : "Program profile: ") + s.pattern, strings)), int(s.index));
    bl->addWidget(base_);
    bl->addWidget(wordsLabel(baseBox));
    qobject_cast<QLabel *>(bl->itemAt(1)->widget())->setText(T("A copy of the settings that profile holds itself (its joystick mode, bindings, tuning). It does not follow the profile afterwards."));
    l->addWidget(baseBox);

    // 3. joystick mode
    auto *modeBox = new QGroupBox(T("Joystick mode"), content);
    auto *ml = new QVBoxLayout(modeBox);
    mode_ = new QComboBox(modeBox);
    mode_->setObjectName(QStringLiteral("builderMode"));
    mode_->setAccessibleName(T("Joystick mode"));
    mode_->addItem(T("None of its own: the profile follows the joystick mode of Global"));
    modeIds_.push_back(std::string());
    for (const SettingsModel::ModeOption &o : m.modeOptions(0)) {
        if (o.id.empty() || !o.known) continue;
        mode_->addItem(qs8(o.label) + (o.label != o.id ? QStringLiteral(" (") + qs8(o.id) + QLatin1Char(')') : QString()));
        modeIds_.push_back(o.id);
    }
    modeCopy_ = new QCheckBox(T("Make my own copy of this joystick mode"), modeBox);
    modeCopy_->setObjectName(QStringLiteral("builderModeCopy"));
    modeCopy_->setEnabled(false);
    modeWords_ = wordsLabel(modeBox, QStringLiteral("builderModeWords"));
    ml->addWidget(mode_);
    ml->addWidget(modeCopy_);
    ml->addWidget(modeWords_);
    l->addWidget(modeBox);

    // 4. button modes per button
    auto *btnBox = new QGroupBox(T("Button modes"), content);
    auto *bbl = new QVBoxLayout(btnBox);
    auto *btnHelp = wordsLabel(btnBox);
    btnHelp->setText(T("Give single buttons a button mode. By default the profile follows the button mode by name; tick the copy to give it its own."));
    bbl->addWidget(btnHelp);
    buttonRows_ = new QVBoxLayout;
    bbl->addLayout(buttonRows_);
    auto *addBtn = new QPushButton(T("Add a button mode ..."), btnBox);
    addBtn->setObjectName(QStringLiteral("builderAddButton"));
    bbl->addWidget(addBtn, 0, Qt::AlignLeft);
    l->addWidget(btnBox);

    // 5. single motions and buttons of other profiles
    auto *takeBox = new QGroupBox(T("Take over from other profiles"), content);
    auto *tl = new QVBoxLayout(takeBox);
    auto *takeHelp = wordsLabel(takeBox);
    takeHelp->setText(T("Copy single motions or buttons from another profile or a joystick mode, whatever they are set to there. A copy: later changes in the source do not follow."));
    tl->addWidget(takeHelp);
    motionRows_ = new QVBoxLayout;
    takeButtonRows_ = new QVBoxLayout;
    tl->addLayout(motionRows_);
    tl->addLayout(takeButtonRows_);
    auto *takeBtns = new QHBoxLayout;
    auto *addMotion = new QPushButton(T("Add a motion ..."), takeBox);
    addMotion->setObjectName(QStringLiteral("builderAddMotion"));
    auto *addTakeButton = new QPushButton(T("Add a button ..."), takeBox);
    addTakeButton->setObjectName(QStringLiteral("builderAddTakeButton"));
    takeBtns->addWidget(addMotion);
    takeBtns->addWidget(addTakeButton);
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
    buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
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
    connect(choose, &QPushButton::clicked, this, [this] { choosePattern(); });
    connect(addBtn, &QPushButton::clicked, this, [this] { addButtonRow(); });
    connect(addMotion, &QPushButton::clicked, this, [this] { addMotionRow(); });
    connect(addTakeButton, &QPushButton::clicked, this, [this] { addTakeButtonRow(); });
    validate();
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

void ProfileBuilderDialog::addButtonRow() {
    const ButtonModeSet *bm = ctx_.buttonModes ? &ctx_.buttonModes->set() : nullptr;
    if (!bm || bm->modes.empty()) return;
    ButtonRow r;
    r.box = new QWidget(this);
    auto *h = new QHBoxLayout(r.box);
    h->setContentsMargins(0, 0, 0, 0);
    r.number = new QSpinBox(r.box);
    r.number->setObjectName(QStringLiteral("builderButtonNumber"));
    r.number->setAccessibleName(T("Button"));
    r.number->setPrefix(T("Button") + QLatin1Char(' '));
    r.number->setRange(1, ctx_.settings->buttonLimit());
    r.mode = new QComboBox(r.box);
    r.mode->setObjectName(QStringLiteral("builderButtonMode"));
    r.mode->setAccessibleName(T("Button mode"));
    for (const ButtonMode &b : bm->modes) r.mode->addItem(qs8(b.label.empty() ? b.id : b.label) + QStringLiteral(" (") + qs8(b.id) + QLatin1Char(')'), qs8(b.id));
    r.copy = new QCheckBox(T("Own copy"), r.box);
    r.copy->setObjectName(QStringLiteral("builderButtonCopy"));
    r.copy->setToolTip(T("Make my own copy of this button mode"));
    auto *rm = new QPushButton(T("Remove"), r.box);
    h->addWidget(r.number);
    h->addWidget(r.mode, 1);
    h->addWidget(r.copy);
    h->addWidget(rm);
    buttonRows_->addWidget(r.box);
    QWidget *box = r.box;
    connect(rm, &QPushButton::clicked, this, [this, box] {
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i].box == box) buttons_.erase(buttons_.begin() + long(i));
        box->hide();
        box->deleteLater();
        validate();
    });
    connect(r.number, &QSpinBox::valueChanged, this, [this] { validate(); });
    connect(r.mode, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    connect(r.copy, &QCheckBox::toggled, this, [this] { validate(); });
    buttons_.push_back(r);
    validate();
}

void ProfileBuilderDialog::addMotionRow() {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    MotionTakeRow r;
    r.box = new QWidget(this);
    auto *h = new QHBoxLayout(r.box);
    h->setContentsMargins(0, 0, 0, 0);
    r.motion = new QComboBox(r.box);
    r.motion->setObjectName(QStringLiteral("builderMotion"));
    r.motion->setAccessibleName(T("Motion"));
    for (const spn::ui::MotionRow &mr : profileAssignment(*ctx_.settings, 0, strings).motions) {
        if (mr.id == "lift") continue;
        r.motion->addItem(qs8(mr.name), qs8(mr.id));
    }
    r.source = new QComboBox(r.box);
    r.source->setObjectName(QStringLiteral("builderMotionSource"));
    r.source->setAccessibleName(T("Take over from"));
    for (const BorrowSource &s : motionSources_) r.source->addItem(qs8(sourceLabel(s.label, strings)));
    auto *rm = new QPushButton(T("Remove"), r.box);
    h->addWidget(r.motion, 1);
    h->addWidget(r.source, 2);
    h->addWidget(rm);
    motionRows_->addWidget(r.box);
    QWidget *box = r.box;
    connect(rm, &QPushButton::clicked, this, [this, box] {
        for (size_t i = 0; i < motions_.size(); ++i)
            if (motions_[i].box == box) motions_.erase(motions_.begin() + long(i));
        box->hide();
        box->deleteLater();
        validate();
    });
    connect(r.motion, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    connect(r.source, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    motions_.push_back(r);
    validate();
}

void ProfileBuilderDialog::addTakeButtonRow() {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    TakeButtonRow r;
    r.box = new QWidget(this);
    auto *h = new QHBoxLayout(r.box);
    h->setContentsMargins(0, 0, 0, 0);
    r.number = new QSpinBox(r.box);
    r.number->setObjectName(QStringLiteral("builderTakeButtonNumber"));
    r.number->setAccessibleName(T("Button"));
    r.number->setPrefix(T("Button") + QLatin1Char(' '));
    r.number->setRange(1, ctx_.settings->buttonLimit());
    r.source = new QComboBox(r.box);
    r.source->setObjectName(QStringLiteral("builderTakeButtonSource"));
    r.source->setAccessibleName(T("Take over from"));
    for (const BorrowSource &s : buttonSources_) r.source->addItem(qs8(sourceLabel(s.label, strings)));
    auto *rm = new QPushButton(T("Remove"), r.box);
    h->addWidget(r.number);
    h->addWidget(r.source, 1);
    h->addWidget(rm);
    takeButtonRows_->addWidget(r.box);
    QWidget *box = r.box;
    connect(rm, &QPushButton::clicked, this, [this, box] {
        for (size_t i = 0; i < takeButtons_.size(); ++i)
            if (takeButtons_[i].box == box) takeButtons_.erase(takeButtons_.begin() + long(i));
        box->hide();
        box->deleteLater();
        validate();
    });
    connect(r.number, &QSpinBox::valueChanged, this, [this] { validate(); });
    connect(r.source, &QComboBox::currentIndexChanged, this, [this] { validate(); });
    takeButtons_.push_back(r);
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
    problem_->setText(problem.isEmpty() ? buildError_ : mark(StatusKind::Error, problem));
    if (!problem.isEmpty()) buildError_.clear();
    ok_->setEnabled(!b.name.empty() && problem.isEmpty());

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
