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

class QComboBox;
class QLabel;

class TakeOverDialog : public QDialog {
    Q_OBJECT
public:
    // words(i): what source i would give, in the user's language (borrowedMotionWords / borrowedButtonWords).
    TakeOverDialog(const QString &title, const QString &intro, const std::vector<spn::ui::BorrowSource> &sources,
                   std::function<std::string(size_t)> words, const spn::ui::StringProvider &strings, QWidget *parent = nullptr);
    int selected() const;  // index into the sources given (valid after exec() == Accepted), -1 = none
    void select(int index);

private:
    QComboBox *combo_ = nullptr;
    QLabel *words_ = nullptr;
    std::function<std::string(size_t)> wordsFn_;
};
