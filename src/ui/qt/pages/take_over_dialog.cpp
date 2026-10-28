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
    combo_ = new QComboBox(this);
    combo_->setObjectName(QStringLiteral("takeOverSource"));
    combo_->setAccessibleName(T("Take over from"));
    for (const spn::ui::BorrowSource &s : sources) combo_->addItem(qs8(spn::ui::sourceLabel(s.label, strings)));
    l->addWidget(combo_);
    words_ = new QLabel(this);
    words_->setObjectName(QStringLiteral("takeOverWords"));
    words_->setWordWrap(true);
    words_->setTextFormat(Qt::PlainText);
    words_->setAccessibleName(T("What would be copied"));
    words_->setMinimumWidth(420);
    l->addWidget(words_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(T("Take over"));
    buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!sources.empty());
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
