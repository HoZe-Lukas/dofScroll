// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Factory button counts per 3Dconnexion model.
#pragma once
#include <string>

namespace spn {

// Returns the number of physical buttons of the device model identified by the
// reported device name (case-insensitive substring match against a table of
// models whose counts were verified against official/manufacturer product
// pages). For unknown models, or when the name is empty, the count reported by
// the driver (`reported`) is returned unchanged. The result is never negative.
int factoryButtonCount(const std::string &deviceName, int reported);

}  // namespace spn
