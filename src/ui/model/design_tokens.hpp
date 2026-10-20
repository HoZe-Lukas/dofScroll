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

constexpr int kIconSize = 20;        // icons on the tabs
constexpr int kMinWindowWidth = 640; // smallest usable window (laptop with large fonts)
constexpr int kMinWindowHeight = 420;
// Pages made of cards and forms (Overview, Channels) stop growing at this width and stay centered
// on a very wide window, so that lines and rows do not spread over the whole screen.
constexpr int kMaxContentWidth = 1100;

}  // namespace spn::ui::tokens
