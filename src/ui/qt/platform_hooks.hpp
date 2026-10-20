// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform seams of the UI that are not part of the toolkit-free models.
#pragma once
#include <functional>
#include <string>

namespace dofscroll_ui {

// Platform setup that must run before QApplication exists: XInitThreads() in X11 builds (it has to
// be the first Xlib call of the process, the active-window poll runs on a background thread).
void earlyPlatformInit();

// Windows only (no-op elsewhere): dofscroll-ui.exe is a GUI-subsystem program without a console.
// Attaches to the console of the parent process (cmd, PowerShell) and points stdout/stderr at it
// unless they are already redirected, so --help, --version, --self-test and error messages show up.
// Does nothing when the program was not started from a console.
void attachParentConsole();

// Poll function for the program that has the keyboard focus, as the string an [app:*] profile is
// matched against (same rule as the daemon's ActiveWindowProvider). Never blocks. Empty
// std::function when this build/platform cannot tell (the capture button is then disabled).
std::function<std::string()> makeActiveWindowPoll();

}  // namespace dofscroll_ui
