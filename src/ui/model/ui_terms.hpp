// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The user-visible names of the two kinds of modes, in one place. The axis behavior (what the
// cap does: scroll, zoom, keys) is always called "Joystick mode"; the named behavior of a single
// button (buttons/*.conf in the configuration folder, buttonmode:<id> in dofscroll.conf) is called
// "Button mode". Config keys and file names do not change with the words (MODE, modes/*.conf).
// These constants are used for the titles of pages, tabs and lists; longer sentences carry the
// words in their text (po/de.po has the translations: "Joystick-Modus", "Tasten-Modus"). To
// rename a kind later, change the constant here and search the old word in src/ui, po/de.po and
// docs (the places are listed in docs/ui.md, section "Terms").
#pragma once

namespace spn::ui::terms {

constexpr const char *kJoystickMode = "Joystick mode";
constexpr const char *kJoystickModes = "Joystick modes";
constexpr const char *kButtonMode = "Button mode";
constexpr const char *kButtonModes = "Button modes";

}  // namespace spn::ui::terms
