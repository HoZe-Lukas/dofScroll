// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// One button row, shared by the Profiles page and the joystick mode editor: "Button n", the
// drop-down (Default (inherit) / button modes / actions, see button_choice_model.hpp), the argument
// picker (key or mode) and the reset button. The row only presents the model; every change goes
// through the handlers, which write to whatever the page edits (a profile, a mode form).
#pragma once
#include <QString>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ui/model/binding_editor_model.hpp"
#include "ui/model/button_choice_model.hpp"
#include "ui/model/strings.hpp"

class QGridLayout;
class QObject;
class QWidget;

struct ButtonRowEnv {
    spn::ui::BindingContext context = spn::ui::BindingContext::Specific;
    bool advancedOpen = false;
    const spn::ButtonModeSet *buttonModes = nullptr;
    std::vector<std::string> modeIds;  // offered by "Switch to mode" and the like
    std::map<std::string, std::string> modeTips;  // id -> description of the joystick mode (tooltip of its entry)
    const spn::ui::StringProvider *strings = nullptr;
    int deviceButtons = 0;      // > 0: the "Button n" label says in its tooltip how many buttons the device has
    bool showInherited = true;  // inherited rows show what they inherit (profiles); a mode has nothing to show
    QString resetText, resetTip;
};

struct ButtonRowHandlers {
    std::function<void(const spn::ui::ButtonChoice &)> choose;  // an entry of the drop-down was picked
    std::function<void(const spn::ui::Binding &)> setArgument;  // key or mode argument changed
    std::function<void()> reset;                                // "Inherit" / "Unbind"
};

// Adds the widgets of one row to grid row r (columns 0..2). Connections live as long as owner.
void addButtonRow(QObject *owner, QWidget *parent, QGridLayout *grid, int r, const spn::ui::BindingRow &row,
                  const ButtonRowEnv &env, const ButtonRowHandlers &handlers);
