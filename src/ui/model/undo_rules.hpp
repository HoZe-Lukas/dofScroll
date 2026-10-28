// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Rules of the Undo offers of the interface (profile removal, axis assistant Apply). No toolkit.
//
// There is NO time limit: an offer stays until something else makes it obsolete (another change of the profile
// list, the next Apply or Undo, a reload of the configuration from disk, the Global values changed by hand) or the
// user presses Dismiss. Nothing in this header or in the pages that use it looks at a clock; a slow or
// keyboard-only user has all the time they need.
#pragma once

namespace spn::ui {

// Ctrl+Z (Cmd+Z on macOS) triggers the visible Undo only when an undo is on offer and the keyboard focus is not in
// a field that has an undo of its own (line edit, spin box, text edit, editable combo box).
inline bool undoShortcutApplies(bool undoOnOffer, bool focusInTextInput) { return undoOnOffer && !focusInTextInput; }

}  // namespace spn::ui
