// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/profile_motions_view.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <set>

#include "key_combo.hpp"
#include "key_engine_box.hpp"
#include "pages/motion_editor.hpp"
#include "pages/take_over_dialog.hpp"
#include "side_picker.hpp"
#include "ui/model/borrow_model.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/device_layout_model.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
// SCROLL_V_, SCROLL_H_, ZOOM_ or LIFT_ for the motion of the picture.
std::string channelPrefix(const std::string &motion) {
    if (motion == "v_push" || motion == "v_tilt") return "SCROLL_V_";
    if (motion == "h_push" || motion == "h_tilt") return "SCROLL_H_";
    if (motion == "twist") return "ZOOM_";
    if (motion == "lift") return "LIFT_";
    return std::string();
}
}  // namespace

ProfileMotionsView::ProfileMotionsView(const UiContext &ctx, ButtonRowFn addButtonRow, QWidget *parent)
    : QWidget(parent), ctx_(ctx), addButtonRow_(std::move(addButtonRow)) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    extras_ = new QVBoxLayout;
    outer->addLayout(extras_);
    auto *root = new QBoxLayout(QBoxLayout::LeftToRight);
    root_ = root;
    outer->addLayout(root, 1);
    auto *left = new QVBoxLayout;
    picture_ = new MotionPicture(this);
    left->addWidget(picture_);
    sentence_ = new QLabel(this);
    sentence_->setWordWrap(true);
    sentence_->setTextFormat(Qt::PlainText);
    sentence_->setAccessibleName(T("What the selected item does"));
    left->addWidget(sentence_);
    left->addStretch(1);
    root->addLayout(left, 2);
    inspector_ = new QGroupBox(T("Selected"), this);
    col_ = new QVBoxLayout(inspector_);
    root->addWidget(inspector_, 3);
    connect(picture_, &MotionPicture::selected, this, [this](const QString &id) {
        selected_ = id;
        rebuildInspector();
    });
}

// Picture and inspector side by side only while both fit with what they really need (their minimum widths, which depend on
// the font and its glyph widths, not on the line height); otherwise the inspector goes under the picture and uses the whole
// width, so no text is cut off and no horizontal scroll bar is needed. Also called after the inspector was rebuilt: its
// minimum width changes with its content and with the font, without any resize.
void ProfileMotionsView::updateDirection() {
    if (!root_ || root_->count() < 2) return;
    const int em = fontMetrics().height();
    const int need = root_->itemAt(0)->minimumSize().width() + std::max(root_->itemAt(1)->minimumSize().width(), 28 * em) + root_->spacing() + 16;
    const QBoxLayout::Direction want = width() >= need ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (root_->direction() != want) root_->setDirection(want);
}

void ProfileMotionsView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updateDirection();
}

QString ProfileMotionsView::selectedId() const { return picture_->current(); }
QString ProfileMotionsView::sentence() const { return sentence_->text(); }

bool ProfileMotionsView::select(const QString &id) {
    for (const MotionPicture::Item &it : picture_->items())
        if (it.id == id) {
            picture_->setCurrent(id);
            return true;
        }
    return false;
}

void ProfileMotionsView::setExtras(const QList<QWidget *> &widgets) {
    clearLayoutNow(extras_);
    for (QWidget *w : widgets)
        if (w) extras_->addWidget(w);
}

void ProfileMotionsView::setSection(size_t section, bool bindingEditor) {
    section_ = section;
    bindingEditor_ = bindingEditor;
    rebuild();
}

void ProfileMotionsView::rebuild() {
    if (!ctx_.settings) return;
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const ProfileAssignment a = profileAssignment(*ctx_.settings, section_, strings);
    QList<MotionPicture::Item> items;
    for (const MotionRow &r : a.motions) {
        MotionPicture::Item it;
        it.id = QStringLiteral("m:") + qs8(r.id);
        it.title = qs8(r.name);
        it.text = qs8(r.action);
        it.muted = r.muted();
        it.tooltip = qs8(r.name) + QStringLiteral(" (") + qs8(r.how) + QStringLiteral("): ") + qs8(r.action);
        items << it;
    }
    const std::string device = ctx_.status ? ctx_.status->status().device : std::string();
    const DeviceLayout layout = deviceLayout(device, reportedOrDefaultButtons(ctx_.settings->buttonLimit()));
    std::set<int> have;
    auto addButton = [&](int n, double ux, double uy) {
        have.insert(n);
        const ButtonChip *chip = nullptr;
        for (const ButtonChip &c : a.buttons)
            if (c.number == n) chip = &c;
        if (buttonFilter_ && !buttonFilter_(n, T("Button") + QStringLiteral(" %1 ").arg(n) + (chip ? qs8(chip->action) : QString())))
            return;
        MotionPicture::Item it;
        it.id = QStringLiteral("b:") + QString::number(n);
        it.title = QString::number(n);
        it.text = chip ? qs8(chip->action) : T("Unassigned");
        it.muted = !chip || chip->muted;
        it.ux = ux;
        it.uy = uy;
        it.tooltip = chip ? qs8(chip->tooltip) : T("Button %1: not assigned").arg(n);
        items << it;
    };
    for (const ButtonSlot &s : layout.buttons) addButton(s.number, s.x, s.y);
    for (const ButtonChip &c : a.buttons)
        if (!have.count(c.number)) addButton(c.number, 0.5, 1.0);
    picture_->setItems(items, layout.kind);
    if (!selected_.isEmpty()) picture_->setCurrent(selected_);
    rebuildInspector();
}

void ProfileMotionsView::rebuildInspector() {
    clearLayoutNow(col_);
    title_ = nullptr;
    const QString id = picture_->current();
    if (id.startsWith(QLatin1String("m:"))) buildMotion(col_, id.mid(2));
    else if (id.startsWith(QLatin1String("b:"))) buildButton(col_, id.mid(2).toInt());
    col_->addStretch(1);
    QTimer::singleShot(0, this, [this] {
        updateDirection();
        if (inspectorBuilt) inspectorBuilt();
    });
}

void ProfileMotionsView::buildMotion(QVBoxLayout *col, const QString &motionId) {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const ProfileAssignment a = profileAssignment(*ctx_.settings, section_, strings);
    const MotionRow *row = nullptr;
    for (const MotionRow &r : a.motions)
        if (qs8(r.id) == motionId) row = &r;
    if (!row) return;
    title_ = new QLabel(QStringLiteral("<b>") + qs8(row->name).toHtmlEscaped() + QStringLiteral("</b>"), inspector_);
    title_->setTextFormat(Qt::RichText);
    title_->setWordWrap(true);
    col->addWidget(title_);
    auto *how = new QLabel(qs8(row->how), inspector_);
    how->setWordWrap(true);
    col->addWidget(how);
    QString text = qs8(row->name) + QStringLiteral(": ") + qs8(row->action);
    sentence_->setText(text);
    auto *belongs = new QLabel(T("The joystick mode %1 gives this motion its action. Below you can set another action in this profile only; the joystick mode itself is changed on the Joystick modes page.").arg(qs8(a.modeLabel)), inspector_);
    belongs->setWordWrap(true);
    belongs->setTextFormat(Qt::PlainText);
    col->addWidget(belongs);

    // What this profile does with the motion: inherited, set here or cleared, in words, with the three ways to change it
    // (motion_edit_model.hpp: Clear writes the action "none" of this motion into this profile only, "Use the inherited setting" removes it again).
    const MotionEdit edit = motionEdit(*ctx_.settings, section_, motionId.toStdString(), strings);
    if (edit.editable) {
        // A group box title cannot wrap and sets a minimum width of its own: the title is a wrapping label inside the box.
        const QString stateTitle = section_ == 0 ? T("In Global") : T("In this profile");
        auto *state = new QGroupBox(inspector_);
        state->setAccessibleName(stateTitle);
        state->setObjectName(QStringLiteral("motionState"));
        auto *sl = new QVBoxLayout(state);
        auto *stateHead = new QLabel(QStringLiteral("<b>") + stateTitle.toHtmlEscaped() + QStringLiteral("</b>"), state);
        stateHead->setTextFormat(Qt::RichText);
        stateHead->setWordWrap(true);
        sl->addWidget(stateHead);
        auto *words = new QLabel(qs8(edit.words), state);
        words->setObjectName(QStringLiteral("motionStateWords"));
        words->setWordWrap(true);
        words->setTextFormat(Qt::PlainText);
        words->setAccessibleName(T("State of this motion"));
        sl->addWidget(words);
        // Another action for this motion in this profile: the choices of the joystick mode editor (motionActionChoices),
        // written as the action key of the motion; an invalid value is shown here and never written.
        auto form = std::make_shared<ChannelForm>(edit.action);
        auto *kindLabel = new QLabel(T("What this motion does:"), state);
        auto *kind = new QComboBox(state);
        kind->setObjectName(QStringLiteral("motionAction"));
        kind->setAccessibleName(T("What this motion does in this profile"));
        // A combo box reports its current text as its name: the purpose is the description, and the visible label is its buddy.
        kind->setAccessibleDescription(T("Action for %1").arg(qs8(row->name)));
        kindLabel->setBuddy(kind);
        kindLabel->setWordWrap(true);
        kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);  // long entries are elided, never widen the inspector
        kind->setMinimumContentsLength(12);
        kind->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        kind->setMinimumWidth(120);
        kind->setToolTip(T("Action for %1").arg(qs8(row->name)));
        int current = 0;
        const bool isLift = motionId == QLatin1String("lift");
        // Lift / press: nothing is its default and a pinned "none" shows as that entry; no analog keys in its list.
        const ChannelActionType shown = isLift && edit.action.type == ChannelActionType::Off ? ChannelActionType::Default : edit.action.type;
        for (const MotionActionChoice &c : isLift ? liftActionChoices() : motionActionChoices()) {
            if (c.key() == choiceIdOf(ChannelForm{shown, edit.action.neg, edit.action.pos})) current = kind->count();
            kind->addItem(T(c.label), c.key());
        }
        kind->setCurrentIndex(current);
        sl->addWidget(kindLabel);
        sl->addWidget(kind);
        // Any motion can get any action; what cannot work here is greyed out with the reason, and what is worth knowing
        // about the chosen action (zoom from a tilt) is said in words below the list (motionChoiceCheck).
        auto *choiceNote = new QLabel(state);
        choiceNote->setObjectName(QStringLiteral("motionChoiceNote"));
        choiceNote->setWordWrap(true);
        choiceNote->setTextFormat(Qt::PlainText);
        choiceNote->setAccessibleName(T("About this action"));
        {
            auto *model = qobject_cast<QStandardItemModel *>(kind->model());
            const std::string motionStd = motionId.toStdString();
            for (int i = 0; i < kind->count(); ++i) {
                const MotionChoiceCheck c = motionChoiceCheck(*ctx_.settings, section_, motionStd, choiceType(kind->itemData(i).toInt()), strings);
                if (model && model->item(i)) {
                    model->item(i)->setEnabled(c.works || i == current);
                    if (!c.works && i != current) model->item(i)->setToolTip(qs8(c.note));
                }
                if (i == current && !c.note.empty()) choiceNote->setText(qs8(c.note));
            }
        }
        choiceNote->setVisible(!choiceNote->text().isEmpty());
        sl->addWidget(choiceNote);
        auto *keysBox = new QWidget(state);
        auto *keysLayout = new QVBoxLayout(keysBox);
        keysLayout->setContentsMargins(0, 0, 0, 0);
        keysBox->setVisible(edit.action.usesKeys());
        sl->addWidget(keysBox);
        const std::string motion = motionId.toStdString();
        auto applyForm = [this, form, motion, words]() {
            std::string err;
            if (!setMotionAction(*ctx_.settings, section_, motion, *form, &err)) {
                words->setText(mark(StatusKind::Error, T("Invalid value: ") + qs8(err)));
                return;
            }
            ctx_.saveSoon();
            if (changed) changed();
            rebuild();
        };
        if (edit.action.usesKeys()) {
            // Step and hold take a mouse click per side as well (the Mouse branch of the key list); the analog keys: do not.
            const bool mouseSides = edit.action.type != ChannelActionType::Keys;
            const bool holds = edit.action.type == ChannelActionType::Hold;
            const QString negText = mouseSides ? (holds ? T("Key or mouse button held for the negative direction") : T("Key or mouse click for the negative direction"))
                                               : T("Key for the negative direction");
            const QString posText = mouseSides ? (holds ? T("Key or mouse button held for the positive direction") : T("Key or mouse click for the positive direction"))
                                               : T("Key for the positive direction");
            QWidget *previous = kind;
            for (int side = 0; side < 2; ++side) {
                std::string *chord = side == 0 ? &form->neg : &form->pos;
                const QString sideText = side == 0 ? negText : posText;
                auto *label = new QLabel(sideText, keysBox);
                label->setWordWrap(true);
                keysLayout->addWidget(label);
                QWidget *picker = nullptr;
                KeyTreePicker *kc = nullptr;
                picker = sidePicker(keysBox, this, chord, qs8(row->name) + QStringLiteral(": ") + sideText, applyForm, &kc, mouseSides);
                label->setBuddy(kc);
                keysLayout->addWidget(picker);
                QWidget::setTabOrder(previous, picker);
                previous = picker;
            }
            // analog or digital, the rate and trigger fields of this motion (stage 3 of #224)
            KeyEngineEnv env;
            env.motionId = motion_edit_detail::keyTuningMotion(*ctx_.settings, section_, motion);
            env.motionName = qs8(row->name);
            env.form = form.get();
            env.effective = ctx_.settings->effectiveWithMode(section_);
            const size_t section = section_;
            SettingsModel *model = ctx_.settings;
            env.get = [model, section](const std::string &key) {
                std::string held;
                motion_edit_detail::ownText(*model, section, key, &held);
                return held;
            };
            env.set = [model, section](const std::string &key, const std::string &value, std::string *err) {
                if (value.empty()) return model->clearOverride(section, key);
                return model->setValue(section, key, value, err);
            };
            env.commit = applyForm;
            env.typed = [this] {
                ctx_.saveTyped();
                if (changed) changed();
            };
            env.flush = [this] { ctx_.saveSoon(); };
            if (QWidget *engine = keyEngineBox(keysBox, this, env, strings)) keysLayout->addWidget(engine);
        }
        connect(kind, &QComboBox::activated, this, [kind, form, applyForm](int idx) {
            applyChoice(form.get(), kind->itemData(idx).toInt());  // the sides keep what they hold when the new kind can use it
            applyForm();
        });
        auto *buttons = new QVBoxLayout;  // one below the other: three buttons side by side need more than a narrow inspector has
        auto *clear = new QPushButton(T("Clear"), state);
        clear->setObjectName(QStringLiteral("motionClear"));
        clear->setEnabled(edit.canClear);
        clear->setToolTip(T("Nothing happens for this motion in this profile; everything else stays inherited"));
        auto *on = new QPushButton(T("Switch on"), state);
        on->setObjectName(QStringLiteral("motionSwitchOn"));
        on->setVisible(edit.canSwitchOn);
        on->setToolTip(T("Make this motion do something again in this profile"));
        auto *inherit = new QPushButton(section_ == 0 ? T("Use the default") : T("Use the inherited setting"), state);
        inherit->setObjectName(QStringLiteral("motionInherit"));
        inherit->setEnabled(edit.canInherit);
        inherit->setToolTip(T("Remove what this profile sets for this motion: the inherited setting applies again"));
        auto *takeOver = new QPushButton(T("Take over from ..."), state);
        takeOver->setObjectName(QStringLiteral("motionTakeOver"));
        takeOver->setToolTip(T("Copy what this motion does in another profile or joystick mode into this profile"));
        takeOver->setVisible(!isLift);  // taking the lift over is not offered yet
        connect(takeOver, &QPushButton::clicked, this, [this, motionId] { takeOverMotion(motionId); });
        for (QPushButton *b : {clear, on, inherit, takeOver}) {
            b->setMinimumHeight(24);
            b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long text must not widen the inspector
            b->setMinimumWidth(120);
            buttons->addWidget(b, 0, Qt::AlignLeft);
        }
        sl->addLayout(buttons);
        sl->addWidget(takeOver, 0, Qt::AlignLeft);  // its own line: the row above is already as wide as a narrow inspector allows
        const std::string id = motionId.toStdString();
        auto run = [this, id, words](int what) {
            std::string err;
            bool ok = false;
            if (what == 0) ok = clearMotion(*ctx_.settings, section_, id, &memory_, &err);
            else if (what == 1) ok = switchOnMotion(*ctx_.settings, section_, id, &memory_, &err);
            else ok = inheritMotion(*ctx_.settings, section_, id, &memory_);
            if (!ok) {
                if (!err.empty()) words->setText(mark(StatusKind::Error, qs8(err)));
                return;
            }
            ctx_.saveSoon();
            if (changed) changed();
            rebuild();
        };
        connect(clear, &QPushButton::clicked, this, [run] { run(0); });
        connect(on, &QPushButton::clicked, this, [run] { run(1); });
        connect(inherit, &QPushButton::clicked, this, [run] { run(2); });
        col->addWidget(state);
    }

    const std::string prefix = channelPrefix(motionId.toStdString());
    if (prefix.empty()) return;
    auto *box = new QGroupBox(inspector_);
    box->setAccessibleName(T("Tuning in this profile"));
    auto *g = new QGridLayout(box);
    g->setColumnStretch(1, 1);
    auto *boxHead = new QLabel(QStringLiteral("<b>") + T("Tuning in this profile").toHtmlEscaped() + QStringLiteral("</b>"), box);
    boxHead->setTextFormat(Qt::RichText);
    boxHead->setWordWrap(true);
    g->addWidget(boxHead, 0, 0, 1, 3);
    static const char *const fields[][2] = {{"SENS", "Sensitivity"}, {"DEADZONE", "Dead zone"}, {"SIGN", "Direction (sign)"},
                                            {"CURVE", "Curve"}, {"SMOOTH_MS", "Smoothing (ms)"}};
    int r = 1;
    for (const auto &f : fields) {
        const std::string key = prefix + f[0];
        SettingsModel::Value v;
        if (!ctx_.settings->value(section_, key, &v)) continue;
        auto *label = new QLabel(T(f[1]) + QLatin1Char(':'), box);
        auto *lineEdit = new QLineEdit(qs8(v.text), box);
        lineEdit->setAccessibleName(T(f[1]));
        lineEdit->setToolTip(qs8(key));
        auto *origin = new QLabel(qs8(originBadgeText(keyOrigin(*ctx_.settings, section_, key), strings)), box);
        origin->setWordWrap(true);
        origin->setTextFormat(Qt::PlainText);
        {   // the layouts around do not size a wrapped label by its width: reserve the lines it needs at a narrow inspector
            const QFontMetrics fm(origin->font());
            origin->setMinimumHeight(fm.boundingRect(QRect(0, 0, 20 * fm.height(), 10000), Qt::TextWordWrap, origin->text()).height() + 4);
        }
        auto *reset = new QPushButton(section_ == 0 ? T("Reset") : T("Inherit"), box);
        reset->setEnabled(v.overridden);
        reset->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        reset->setMinimumWidth(80);
        reset->setAccessibleName(T(f[1]) + QStringLiteral(": ") + reset->text());
        // the label above its field (and wrapping): a wide font must not widen the inspector
        label->setWordWrap(true);
        label->setBuddy(lineEdit);
        lineEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        lineEdit->setMinimumWidth(80);
        g->addWidget(label, r, 0, 1, 3);
        g->addWidget(lineEdit, r + 1, 0, 1, 2);
        g->addWidget(reset, r + 1, 2);
        g->addWidget(origin, r + 2, 0, 1, 3);
        r += 3;
        const std::string value = v.text;
        connect(lineEdit, &QLineEdit::textEdited, this, [this, key, origin, box](const QString &t) {
            std::string err;
            if (ctx_.settings->setValue(section_, key, t.trimmed().toUtf8().toStdString(), &err)) {
                const IdentityStrings fb;
                origin->setText(qs8(originBadgeText(keyOrigin(*ctx_.settings, section_, key), ctx_.strings ? *ctx_.strings : fb)));
                ctx_.saveTyped();
            } else {
                origin->setText(mark(StatusKind::Error, qs8(err)));
            }
            (void)box;
        });
        connect(reset, &QPushButton::clicked, this, [this, key] {
            ctx_.settings->clearOverride(section_, key);
            ctx_.saveSoon();
            if (changed) changed();
            rebuild();
        });
    }
    col->addWidget(box);
}

void ProfileMotionsView::buildButton(QVBoxLayout *col, int n) {
    title_ = new QLabel(QStringLiteral("<b>") + T("Button %1").arg(n).toHtmlEscaped() + QStringLiteral("</b>"), inspector_);
    title_->setTextFormat(Qt::RichText);
    col->addWidget(title_);
    sentence_->setText(T("Button %1").arg(n));
    {
        auto *scope = new QLabel(T("The binding of this profile. Without one, the Global profile (or the device profile) or the joystick mode applies. Clear makes the button do nothing here."), inspector_);
        scope->setWordWrap(true);
        col->addWidget(scope);
    }
    {   // a joystick mode that binds this button too: say in words who wins (the program profile has the last word)
        const IdentityStrings fallback;
        const std::string note = buttonModeNote(*ctx_.settings, section_, n, ctx_.strings ? *ctx_.strings : fallback);
        if (!note.empty()) {
            auto *modeNote = new QLabel(qs8(note), inspector_);
            modeNote->setObjectName(QStringLiteral("buttonModeNote"));
            modeNote->setWordWrap(true);
            modeNote->setTextFormat(Qt::PlainText);
            col->addWidget(modeNote);
        }
    }
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    col->addLayout(grid);
    if (addButtonRow_) addButtonRow_(inspector_, grid, 0, section_, n);
    // Clear: BUTTON_<n> = none in this profile (explicitly nothing, even when Global or the device binds the button); the
    // drop-down of the row has "Nothing" too, and its way back is the Inherit button of the row.
    auto *clear = new QPushButton(T("Clear"), inspector_);
    clear->setObjectName(QStringLiteral("buttonClear"));
    clear->setToolTip(T("Nothing happens for this button in this profile; everything else stays inherited"));
    connect(clear, &QPushButton::clicked, this, [this, n] {
        std::string err;
        if (ctx_.settings->setButton(section_, n, "none", &err)) {
            ctx_.saveSoon();
            if (changed) changed();
            rebuild();
        }
    });
    auto *takeOver = new QPushButton(T("Take over from ..."), inspector_);
    takeOver->setObjectName(QStringLiteral("buttonTakeOver"));
    takeOver->setToolTip(T("Copy the binding of this button in another profile, joystick mode or button mode into this profile"));
    connect(takeOver, &QPushButton::clicked, this, [this, n] { takeOverButton(n); });
    auto *row = new QHBoxLayout;
    row->addWidget(clear);
    row->addWidget(takeOver);
    row->addStretch(1);
    col->addLayout(row);
}

namespace {
// Shows the reason a take-over was refused in the label of the inspector.
void showRefusal(QWidget *inspector, const std::string &err) {
    if (QLabel *l = inspector->findChild<QLabel *>(QStringLiteral("motionStateWords"))) l->setText(mark(StatusKind::Error, qs8(err)));
}
}  // namespace

bool ProfileMotionsView::takeOverMotion(const QString &motionId) {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const std::string motion = motionId.toStdString();
    const std::vector<BorrowSource> sources = motionSources(*ctx_.settings, section_);
    TakeOverDialog dlg(T("Take over a motion"), T("Copy what this motion does in another profile or joystick mode into this profile. By default only the action is copied; the check box adds the tuning. It is a copy: later changes in the source do not follow, and you can change it here afterwards."),
                       sources, [&](size_t i) { return borrowedMotionWords(*ctx_.settings, sources[i], motion, strings); }, strings, this);
    dlg.offerTuning([&](size_t i, bool tuning) { return borrowedMotionWords(*ctx_.settings, sources[i], motion, strings, tuning); });
    if (dlg.exec() != QDialog::Accepted || dlg.selected() < 0) return false;
    std::string err;
    if (!borrowMotion(*ctx_.settings, section_, motion, sources[size_t(dlg.selected())], &err, std::string(), dlg.tuning())) {
        showRefusal(inspector_, err);
        return false;
    }
    ctx_.saveSoon();
    if (changed) changed();
    rebuild();
    return true;
}

bool ProfileMotionsView::takeOverButton(int n) {
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const ButtonModeSet *bm = ctx_.buttonModes ? &ctx_.buttonModes->set() : nullptr;
    const std::vector<BorrowSource> sources = buttonSources(*ctx_.settings, section_, bm);
    TakeOverDialog dlg(T("Take over a button"), T("Copy the binding of this button in another profile, joystick mode or button mode into this profile. It is a copy: later changes in the source do not follow, and you can change it here afterwards."),
                       sources, [&](size_t i) { return borrowedButtonWords(*ctx_.settings, sources[i], n, bm, strings); }, strings, this);
    if (dlg.exec() != QDialog::Accepted || dlg.selected() < 0) return false;
    std::string err;
    if (!borrowButton(*ctx_.settings, section_, n, sources[size_t(dlg.selected())], &err, bm)) {
        problemText_ = qs8(err);
        ctx_.report(problemText_, true);
        return false;
    }
    ctx_.saveSoon();
    if (changed) changed();
    rebuild();
    return true;
}
