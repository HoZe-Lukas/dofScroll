// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Start at login" behind ONE interface for the daemon and the UI TOGETHER, the same calls on
// Linux, Windows and macOS. The UI (Qt) only uses these two functions and autostartUi():
//   Windows: HKCU Run values `dofscroll` and `dofscroll-ui` (src/platform/win/autostart_win.cpp)
//   macOS:   LaunchAgents io.github.dofscroll.daemon and io.github.dofscroll.ui
//            (src/platform/mac/autostart_mac.cpp)
//   Linux:   `systemctl --user enable|disable` of dofscroll.service and dofscroll-ui.service
//            (src/platform/linux/autostart_linux.cpp)
// The UI program is optional on disk: if it is not installed next to the daemon (unit not found
// on Linux), only the daemon is managed. A missing daemon makes the state Unavailable.
// Nothing here starts or stops a running program: only the next login is affected.
#pragma once

#include <string>

namespace spn {
namespace autostart {

enum class AutostartState {
    Enabled,      // daemon and (if installed) UI both start at login
    Disabled,     // neither starts at login
    Partial,      // a mix (one of the two, or an entry that points elsewhere): toggling fixes it
    Unavailable,  // cannot be changed here; *detail says why (not installed, masked, ...)
    Error         // the state could not be read; *detail has the OS message
};

// Reads the state. *detail (may be null) gets a short reason for Unavailable, Error and Partial.
AutostartState queryAutostart(std::string *detail);

// Enables or disables both entries. true on success; otherwise *err (may be null) has a short
// message. Enabling an already enabled setup is a no-op that repairs entries pointing elsewhere.
bool setAutostart(bool enabled, std::string *err);

}  // namespace autostart
}  // namespace spn
