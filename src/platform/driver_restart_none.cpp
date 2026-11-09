// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux and other systems: no vendor driver to restart (see driver_restart_os.hpp).
#include "driver_restart_os.hpp"

namespace spn::ui {

std::unique_ptr<DriverRestartOps> makeDriverRestartOps() { return nullptr; }
DriverRestartRules osDriverRestartRules() { return DriverRestartRules(); }

}  // namespace spn::ui
