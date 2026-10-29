// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/cycle_editor_widget.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "qt_action_runner.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui_i18n.hpp"

namespace {
enum { kRoleId = Qt::UserRole };
QString qs(const std::string &s) { return QString::fromUtf8(s.c_str(), int(s.size())); }
}  // namespace

CycleEditorWidget::CycleEditorWidget(QWidget *parent) : QGroupBox(T("Cycle through"), parent) {
    setObjectName(QStringLiteral("cycleEditor"));
    setAccessibleName(T("Cycle through"));
    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(spn::ui::tokens::kRowGap);
    setToolTip(T("Which joystick modes the button goes through, in this order, wrapping around."));
    choice_ = new QComboBox(this);
    choice_->addItem(T("All joystick modes"));
    choice_->addItem(T("Only these joystick modes"));
    choice_->setItemData(0, T("All joystick modes, in alphabetical order of their ids"), Qt::ToolTipRole);
    choice_->setAccessibleName(T("Which joystick modes the button goes through"));
    choice_->setAccessibleDescription(T("Which joystick modes the button goes through"));  // the name of a drop-down is its current text
    // a fixed number of characters: the widths of radio buttons and long texts follow the font and made the editor
    // column wider than the window on Windows
    choice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choice_->setMinimumContentsLength(8);
    lay->addWidget(choice_);
    list_ = new QListWidget(this);
    list_->setAccessibleName(T("Joystick modes to cycle through"));
    list_->setToolTip(T("Tick the joystick modes the button goes through; the numbers show the order"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // long names wrap instead of scrolling
    list_->setWordWrap(true);
    list_->setMinimumHeight(110);
    lay->addWidget(list_, 1);
    auto *col = new QHBoxLayout;  // the move buttons below the list (beside it the widget would be too wide)
    up_ = new QToolButton(this);
    down_ = new QToolButton(this);
    up_->setText(QStringLiteral("\u25B2"));
    down_->setText(QStringLiteral("\u25BC"));
    up_->setAccessibleName(T("Move up"));
    down_->setAccessibleName(T("Move down"));
    up_->setToolTip(T("Move the selected ticked joystick mode earlier in the cycle"));
    down_->setToolTip(T("Move the selected ticked joystick mode later in the cycle"));
    col->addWidget(up_);
    col->addWidget(down_);
    col->addStretch(1);
    lay->addLayout(col);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setAccessibleName(T("Order of the cycle"));
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    note_ = new QLabel(qs(spn::ui::CycleEditorModel::builtinNote(QtStrings())), this);
    note_->setWordWrap(true);
    lay->addWidget(summary_);
    lay->addWidget(hint_);
    lay->addWidget(note_);
    setTabOrder(choice_, list_);
    setTabOrder(list_, up_);
    setTabOrder(up_, down_);

    connect(choice_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (!building_) setOnlySelected(i == 1);
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        if (building_) return;
        setChecked(it->data(kRoleId).toString(), it->checkState() == Qt::Checked);
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(up_, &QToolButton::clicked, this, [this] { moveUp(currentRowId()); });
    connect(down_, &QToolButton::clicked, this, [this] { moveDown(currentRowId()); });
}

void CycleEditorWidget::setState(const spn::ModeSet &modes, const std::vector<std::string> &cycle, bool editable) {
    model_.reset(modes, cycle);
    editable_ = editable;
    note_->setVisible(!editable);
    rebuild(QString());
}

void CycleEditorWidget::setOnlySelected(bool only) {
    if (!editable_ || model_.onlySelected() == only) return;
    model_.setOnlySelected(only);
    rebuild(currentRowId());
    emit edited();
}

bool CycleEditorWidget::setChecked(const QString &id, bool on) {
    if (!editable_ || !model_.setChecked(id.toStdString(), on)) {
        rebuild(id);  // a refused tick (full list) is shown as unticked again
        return false;
    }
    if (on && !model_.onlySelected()) model_.setOnlySelected(true);  // ticking a mode means "only these"
    rebuild(id);
    emit edited();
    return true;
}

bool CycleEditorWidget::moveUp(const QString &id) {
    if (!editable_ || !model_.moveUp(id.toStdString())) return false;
    rebuild(id);
    emit edited();
    return true;
}

bool CycleEditorWidget::moveDown(const QString &id) {
    if (!editable_ || !model_.moveDown(id.toStdString())) return false;
    rebuild(id);
    emit edited();
    return true;
}

QString CycleEditorWidget::currentRowId() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? it->data(kRoleId).toString() : QString();
}

QStringList CycleEditorWidget::shownRows() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->text();
    return out;
}

void CycleEditorWidget::rebuild(const QString &keepId) {
    building_ = true;
    {
        const QSignalBlocker b1(list_), b2(choice_);
        choice_->setCurrentIndex(model_.onlySelected() ? 1 : 0);
        list_->clear();
        const QtStrings strings;
        int select = -1, n = 0;
        for (const spn::ui::CycleEditorModel::Row &r : model_.rows()) {
            auto *it = new QListWidgetItem(qs(spn::ui::CycleEditorModel::rowText(r, strings)));
            it->setData(kRoleId, qs(r.id));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(r.checked ? Qt::Checked : Qt::Unchecked);
            list_->addItem(it);
            if (qs(r.id) == keepId) select = n;
            ++n;
        }
        if (select >= 0) list_->setCurrentRow(select);
        summary_->setText(qs(model_.summary(strings)));
        const QString hint = qs(model_.hint(strings));
        hint_->setText(hint);
        hint_->setVisible(!hint.isEmpty());
    }
    building_ = false;
    list_->setEnabled(editable_);  // ticking a mode switches to "only these"
    choice_->setEnabled(editable_);
    updateButtons();
}

void CycleEditorWidget::updateButtons() {
    const std::string id = currentRowId().toStdString();
    const bool on = editable_ && model_.onlySelected();
    up_->setEnabled(on && model_.canMoveUp(id));
    down_->setEnabled(on && model_.canMoveDown(id));
}
