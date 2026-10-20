// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free logic of the "unsaved changes" question: before something replaces a form that has
// edits, the view asks Save / Discard / Cancel. The question itself is a callback (the Qt view shows
// a message box, tests answer directly), so the rule is testable without a window.
#pragma once
#include <functional>
#include <string>

namespace spn::ui {

enum class UnsavedChoice { Save, Discard, Cancel };

// Asks what to do with the unsaved edits of `what` (a user-visible name, UTF-8).
using UnsavedAsk = std::function<UnsavedChoice(const std::string &what)>;

// True if the caller may go on (replace or close the form). Not dirty: yes, without asking. Dirty:
// Save goes on only if `save` succeeded, Discard goes on, Cancel (or no way to ask) stays.
inline bool mayLeaveForm(bool dirty, const std::string &what, const UnsavedAsk &ask, const std::function<bool()> &save) {
    if (!dirty) return true;
    if (!ask) return false;
    switch (ask(what)) {
        case UnsavedChoice::Save: return save && save();
        case UnsavedChoice::Discard: return true;
        case UnsavedChoice::Cancel: break;
    }
    return false;
}

}  // namespace spn::ui
