// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform seams of the UI that are not part of the toolkit-free models.
#pragma once
#include <functional>
#include <string>

namespace dofscroll_ui {

// Poll function for the program that has the keyboard focus, as the string an [app:*] profile is
// matched against (same rule as the daemon's ActiveWindowProvider). Never blocks. Empty
// std::function when this build/platform cannot tell (the capture button is then disabled).
std::function<std::string()> makeActiveWindowPoll();

}  // namespace dofscroll_ui
