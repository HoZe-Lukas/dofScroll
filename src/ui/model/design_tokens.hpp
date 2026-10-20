// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Spacing and size scale of the interface, in device-independent pixels (Qt scales them with the
// display). One place for the numbers, so that margins and gaps follow one scale instead of values
// scattered over the pages. No toolkit.
#pragma once

namespace spn::ui::tokens {

// Spacing scale: every step is a multiple of 4.
constexpr int kSpaceXs = 4;
constexpr int kSpaceSm = 8;
constexpr int kSpaceMd = 12;
constexpr int kSpaceLg = 16;
constexpr int kSpaceXl = 24;

// Layout scale of the whole interface (applied by the application style in src/ui/qt/theme.cpp).
constexpr int kPageMargin = 24;      // margin of a page and of a dialog
constexpr int kSectionGap = 20;      // between group boxes / sections
constexpr int kRowGap = 12;          // between rows of a form, buttons in a row
constexpr int kCardPadding = 18;     // inside a group box or a card
constexpr int kControlHeight = 34;   // minimum height of buttons, inputs and combo boxes
constexpr int kRowHeight = 32;       // rows of lists and trees
constexpr int kRadius = 8;           // corners of buttons, inputs, tabs, scroll bars, menus
constexpr int kRadiusCard = 10;      // corners of cards and group boxes
constexpr int kFontBoostPercent = 108;  // base font relative to the platform's

constexpr int kIconSize = 20;        // icons on the tabs
constexpr int kMinWindowWidth = 720; // smallest usable window (laptop with large fonts)
constexpr int kMinWindowHeight = 480;
constexpr int kDefaultWindowWidth = 1560;  // first start; the saved geometry of ui.conf wins
constexpr int kDefaultWindowHeight = 1040;
// Pages made of cards and forms (Overview, Channels) stop growing at this width and stay centered
// on a very wide window, so that lines and rows do not spread over the whole screen.
constexpr int kMaxContentWidth = 1100;

}  // namespace spn::ui::tokens
