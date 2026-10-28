// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "take_over_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui_i18n.hpp"

TakeOverDialog::TakeOverDialog(const QString &title, const QString &intro, const std::vector<spn::ui::BorrowSource> &sources,
                               std::function<std::string(size_t)> words, const spn::ui::StringProvider &strings, QWidget *parent)
    : QDialog(parent), wordsFn_(std::move(words)) {
    setWindowTitle(title);
    setObjectName(QStringLiteral("takeOverDialog"));
    auto *l = new QVBoxLayout(this);
    auto *head = new QLabel(intro, this);
    head->setWordWrap(true);
    head->setTextFormat(Qt::PlainText);
    l->addWidget(head);
    // a visible label with the combo as its buddy: that is the field's name for a screen reader (a combo box alone
    // is announced by its current text)
    auto *comboLabel = new QLabel(T("Take over from"), this);
    comboLabel->setTextFormat(Qt::PlainText);
    l->addWidget(comboLabel);
    combo_ = new QComboBox(this);
    combo_->setObjectName(QStringLiteral("takeOverSource"));
    combo_->setMinimumContentsLength(24);
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    comboLabel->setBuddy(combo_);
    combo_->setAccessibleName(T("Take over from"));
    combo_->setAccessibleDescription(T("Take over from"));
    for (const spn::ui::BorrowSource &s : sources) combo_->addItem(qs8(spn::ui::sourceLabel(s.label, strings)));
    l->addWidget(combo_);
    words_ = new QLabel(this);
    words_->setObjectName(QStringLiteral("takeOverWords"));
    words_->setWordWrap(true);
    words_->setTextFormat(Qt::PlainText);
    // no accessible name: the text itself is the content and a screen reader reads it
    words_->setMinimumWidth(420);
    l->addWidget(words_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(T("Take over"));
    buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    ok_->setEnabled(!sources.empty());
    l->addWidget(buttons);
    connect(combo_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
}

void TakeOverDialog::refresh() {
    const int i = combo_->currentIndex();
    QString text;
    if (i >= 0 && tuning_ && tuningWordsFn_) text = qs8(tuningWordsFn_(size_t(i), tuning_->isChecked()));
    else if (i >= 0 && wordsFn_) text = qs8(wordsFn_(size_t(i)));
    words_->setText(text);
    fit();
}

// The dialog grows for wrapped text: a label's height depends on its width, which a dialog does not follow by itself.
void TakeOverDialog::fit() {
    if (!layout()) return;
    layout()->activate();
    const int need = layout()->totalHeightForWidth(width());
    if (need > height()) resize(width(), need);
}

void TakeOverDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    fit();
}

void TakeOverDialog::offerTuning(std::function<std::string(size_t, bool)> words) {
    tuningWordsFn_ = std::move(words);
    if (!tuning_) {
        tuning_ = new QCheckBox(T("Also take over the tuning"), this);
        tuning_->setObjectName(QStringLiteral("takeOverTuning"));
        tuning_->setToolTip(T("Also copies sensitivity, dead zone, direction, curve and smoothing of this motion. Off: only the action is copied and the tuning stays as it is here."));
        tuning_->setChecked(false);  // default: the action only
        // above the buttons: after the words
        static_cast<QVBoxLayout *>(layout())->insertWidget(layout()->indexOf(words_) + 1, tuning_);
        // the box was added after the buttons: the Tab order is the order on the screen
        setTabOrder(combo_, tuning_);
        setTabOrder(tuning_, ok_);
        connect(tuning_, &QCheckBox::toggled, this, [this] { refresh(); });
    }
    refresh();
}

bool TakeOverDialog::tuning() const { return tuning_ && tuning_->isChecked(); }
void TakeOverDialog::setTuning(bool on) {
    if (tuning_) tuning_->setChecked(on);
}

int TakeOverDialog::selected() const { return combo_->currentIndex(); }
void TakeOverDialog::select(int index) { combo_->setCurrentIndex(index); }
