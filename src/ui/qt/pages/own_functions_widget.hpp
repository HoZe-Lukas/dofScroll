// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Own functions of this profile" (stage 7a): the functions the profile holds itself for its motions and buttons, made with
// "New function...", listed once each with the motions and buttons that use them. Edit opens the dialog of the first user,
// Delete removes the function from all its users (they inherit again, said in words), "Save as joystick mode" and "Save as
// button mode" write a copy as a mode file (the profile keeps its own function). The logic is ui/model/own_functions_model.hpp.
#pragma once
#include <QWidget>
#include <functional>
#include <vector>

#include "ui/model/own_functions_model.hpp"
#include "ui_context.hpp"

class QLabel;
class QListWidget;
class QPushButton;

class OwnFunctionsWidget : public QWidget {
    Q_OBJECT
public:
    explicit OwnFunctionsWidget(const UiContext &ctx, QWidget *parent = nullptr);
    void setSection(size_t section);  // the profile that is shown; rebuilds the list
    void refresh();                   // the profile changed under the list: redraw it (the last result stays)
    int count() const;

    std::function<void()> changed;  // something was written; the page redraws
    // Edit asks the page to show the motion or button first (the dialog belongs to its inspector).
    std::function<void(const spn::ui::FunctionTarget &)> editRequested;

    // For the self-test: the same as the buttons.
    void selectRow(int row);
    void editSelected();
    void deleteSelected(bool confirmed);
    void saveSelectedAsJoystickMode(const QString &name);
    void saveSelectedAsButtonMode(const QString &name);

private:
    void rebuild();
    void refreshButtons();
    void say(const QString &text, bool error);
    const spn::ui::StringProvider &strings() const;

    const UiContext &ctx_;
    size_t section_ = 0;
    std::vector<spn::ui::OwnFunction> functions_;
    QLabel *empty_ = nullptr, *result_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *edit_ = nullptr, *delete_ = nullptr, *asMode_ = nullptr, *asButton_ = nullptr;
};
