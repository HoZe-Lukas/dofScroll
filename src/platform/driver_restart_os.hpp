// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The operating-system side of the driver restart (ui/model/driver_restart.hpp): which processes the
// signed-in user has, how to close a window, end a process and start a program. One implementation per
// OS (win/driver_restart_win.cpp, mac/driver_restart_mac.cpp); on Linux there is no vendor driver to
// restart (spacenavd is a system service), so the factory returns nothing.
#pragma once
#include <memory>

#include "ui/model/driver_restart.hpp"

namespace spn::ui {

// nullptr where there is nothing to restart (Linux).
std::unique_ptr<DriverRestartOps> makeDriverRestartOps();
// The rules for this system (folders from %ProgramFiles% / $HOME); empty rules where unsupported.
DriverRestartRules osDriverRestartRules();
// true on Windows and macOS.
inline bool driverRestartAvailable() { return !osDriverRestartRules().serviceName.empty(); }

}  // namespace spn::ui
