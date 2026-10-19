// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Page registry of the settings window: every page registers itself with ONE line in
// page_registry.cpp. A page is created from the shared PageContext.
#pragma once
#include <QString>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.hpp"
#include "platform/running_apps.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/settings_model.hpp"

class QWidget;

struct PageContext {
    spn::ui::SettingsModel *settings = nullptr;  // owned by the main window
    spn::ui::ActionRunner *runner = nullptr;
    // Running programs of this OS (never null; may report an error from list()).
    std::function<std::unique_ptr<spn::RunningAppsProvider>()> runningApps;
    // Foreground program as an [app:*] match string; empty function = unsupported here.
    std::function<std::string()> activeWindow;
    // Snapshot of the connected devices (may be empty or an empty function).
    std::function<std::vector<spn::DeviceInfo>()> devices;
};

struct PageEntry {
    const char *id;     // stable identifier
    const char *title;  // English source string of the navigation entry
    QWidget *(*create)(PageContext &ctx, QWidget *parent);
};

// All registered pages in navigation order.
const std::vector<PageEntry> &pageRegistry();
