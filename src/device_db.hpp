// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Device model facts that do not depend on what the hardware or spacenavd reports.
#pragma once

#include <string>

namespace spn {

// Number of buttons the device model has as shipped (factory specification), looked up by the
// device name that spacenavd reports (e.g. "3Dconnexion SpaceMouse Pro"). The UI and the
// validation of BUTTON_<n> bindings MUST use this instead of the reported button count.
// Unknown model -> `reportedButtons` (fallback).
int factoryButtonCount(const std::string &deviceName, int reportedButtons);

}  // namespace spn
