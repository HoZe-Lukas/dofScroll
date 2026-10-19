// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform inputs of the Profiles page and its pickers (kept out of the shared UiContext).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.hpp"
#include "platform/running_apps.hpp"

struct ProfilesEnv {
    // Running programs of this OS (never null; list() may report an error).
    std::function<std::unique_ptr<spn::RunningAppsProvider>()> runningApps;
    // Foreground program as an [app:*] match string; empty function = unsupported here.
    std::function<std::string()> activeWindow;
    // Snapshot of the connected devices (may be empty or an empty function).
    std::function<std::vector<spn::DeviceInfo>()> devices;
};
