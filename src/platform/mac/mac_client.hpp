// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What client_mac.cpp shares with the daemon's mac_platform.cpp.
#pragma once
#include "mac_paths.hpp"

namespace spn {
// <runtimeDir>/reload.sock (or the owner-checked /tmp fallback), where the daemon listens.
ReloadSocket macReloadSocketLocation();
}  // namespace spn
