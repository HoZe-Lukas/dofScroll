// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS frontmost-application provider (implemented in mac_activewin.mm, the only Objective-C++
// file of the daemon). Plain C++ interface so the rest of the daemon stays C++17.
#pragma once
#include <memory>

#include "../platform.hpp"

namespace spn {

// Returns the bundle identifier of the frontmost application (lowercased), or its localized name
// when it has no bundle identifier; "" if unknown.
std::unique_ptr<ActiveWindowProvider> makeMacActiveWindowProvider();

}  // namespace spn
