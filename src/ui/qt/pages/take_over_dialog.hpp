// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Take over from ...": asks where a motion or a button should be copied from (another profile, a joystick mode or,
// for buttons, a button mode) and says in words what would be copied before anything is written. The copy itself is
// made by the model (src/ui/model/borrow_model.hpp); this dialog only chooses the source.
#pragma once
#include <QDialog>
#include <functional>
#include <string>
#include <vector>

#include "ui/model/borrow_model.hpp"
#include "ui/model/strings.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QShowEvent;

class TakeOverDialog : public QDialog {
    Q_OBJECT
public:
    // words(i): what source i would give, in the user's language (borrowedMotionWords / borrowedButtonWords).
    TakeOverDialog(const QString &title, const QString &intro, const std::vector<spn::ui::BorrowSource> &sources,
                   std::function<std::string(size_t)> words, const spn::ui::StringProvider &strings, QWidget *parent = nullptr);
    int selected() const;  // index into the sources given (valid after exec() == Accepted), -1 = none
    void select(int index);
    // The check box "Also take over the tuning" (off: the action only), for a motion. words(i, tuning) then says what would be
    // copied with or without the tuning; it replaces the words function of the constructor.
    void offerTuning(std::function<std::string(size_t, bool)> words);
    bool tuning() const;  // the box is shown and checked
    void setTuning(bool on);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void fit();
    QPushButton *ok_ = nullptr;
    QComboBox *combo_ = nullptr;
    QLabel *words_ = nullptr;
    QCheckBox *tuning_ = nullptr;
    std::function<std::string(size_t, bool)> tuningWordsFn_;
    void refresh();
    std::function<std::string(size_t)> wordsFn_;
};
