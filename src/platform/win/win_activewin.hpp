// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows foreground-program provider (executable name, lowercased): the string [app:*] profiles
// are matched against. Self-contained, so both the daemon and the Qt UI link it.
#pragma once
#include <memory>

#include "../platform.hpp"

namespace spn {

std::unique_ptr<ActiveWindowProvider> makeWinForegroundProvider();

}  // namespace spn
