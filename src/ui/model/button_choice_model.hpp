// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The per-button drop-down of the Profiles page: one list for "what does this button do", in this
// order: "Default (inherit from Global)" (device and program profiles; in [global] "Nothing" instead), the
// button modes (by name), then the plain actions. A referenced button mode that is not installed
// stays selectable as "missing: <id>" and is never rewritten. Toolkit-free; the view only shows it.
#pragma once
#include <string>
#include <vector>

#include "binding_editor_model.hpp"
#include "button_modes.hpp"
#include "strings.hpp"

namespace spn::ui {

struct ButtonChoice {
    enum class Type { Inherit, ButtonMode, Action };
    enum class Group { None, ButtonModes, Actions };
    Type type = Type::Action;
    Group group = Group::None;
    BindingKind kind = BindingKind::None;  // Action
    std::string id;                        // ButtonMode: button mode id
    std::string label;                     // translated
    std::string tooltip;                   // button mode DESCRIPTION; may be empty
    bool missing = false;                  // ButtonMode: no such file
    bool advanced = false;                 // Action: plain key/click, behind "Advanced" in [global]
};

// Entries for one button row. currentId: the button mode the row refers to now (a missing one is
// added, selected and marked). advancedOpen: see bindingKindsFor.
inline std::vector<ButtonChoice> buttonChoices(BindingContext ctx, bool advancedOpen, const ButtonModeSet &bm,
                                               const std::string &currentId, const StringProvider &str) {
    std::vector<ButtonChoice> out;
    if (ctx == BindingContext::Specific) {
        ButtonChoice c;
        c.type = ButtonChoice::Type::Inherit;
        c.label = str.tr("Default (inherit from Global)");
        out.push_back(c);
    } else {
        ButtonChoice c;
        c.kind = BindingKind::None;
        c.label = str.tr("Nothing");
        out.push_back(c);
    }
    for (const ButtonMode &m : bm.modes) {
        ButtonChoice c;
        c.type = ButtonChoice::Type::ButtonMode;
        c.group = ButtonChoice::Group::ButtonModes;
        c.id = m.id;
        c.label = m.label;
        c.tooltip = m.description;
        out.push_back(c);
    }
    if (!currentId.empty() && !bm.find(currentId)) {
        ButtonChoice c;
        c.type = ButtonChoice::Type::ButtonMode;
        c.group = ButtonChoice::Group::ButtonModes;
        c.id = currentId;
        c.label = format(str.tr("missing: %1"), {currentId});
        c.missing = true;
        out.push_back(c);
    }
    for (const BindingKindInfo &k : bindingKindsFor(ctx, advancedOpen)) {
        if (ctx == BindingContext::Global && k.kind == BindingKind::None) continue;  // the first entry
        ButtonChoice c;
        c.group = ButtonChoice::Group::Actions;
        c.kind = k.kind;
        c.label = str.tr(k.label);
        c.advanced = k.advanced;
        out.push_back(c);
    }
    return out;
}

// Index of the entry that shows the row's current state; -1 if it is not offered (for example a
// plain click in [global] while "Advanced" is closed).
inline int selectedButtonChoice(const std::vector<ButtonChoice> &choices, BindingContext ctx, const BindingRow &row) {
    if (ctx == BindingContext::Specific && !row.overridden) return 0;
    for (size_t i = 0; i < choices.size(); ++i) {
        const ButtonChoice &c = choices[i];
        if (row.binding.kind == BindingKind::ButtonMode) {
            if (c.type == ButtonChoice::Type::ButtonMode && c.id == row.binding.arg) return int(i);
        } else if (c.type == ButtonChoice::Type::Action && c.kind == row.binding.kind) {
            return int(i);
        }
    }
    return -1;
}

// What picking an entry writes: Inherit removes the profile's BUTTON_<n> line, a button mode writes
// "buttonmode:<id>", an action goes through withKind (keeping a fitting argument). A missing button
// mode picked again writes the same text, so it is not rewritten to something else.
inline bool applyButtonChoice(SettingsModel &m, size_t section, const BindingRow &row, const ButtonChoice &c,
                              const std::vector<std::string> &modeIds, std::string *err) {
    if (c.type == ButtonChoice::Type::Inherit) {
        m.clearButton(section, row.number);
        return true;
    }
    Binding b;
    if (c.type == ButtonChoice::Type::ButtonMode) {
        b.kind = BindingKind::ButtonMode;
        b.arg = c.id;
    } else {
        b = withKind(row.binding, c.kind, modeIds);
    }
    return applyBinding(m, section, row.number, b, err);
}

}  // namespace spn::ui
