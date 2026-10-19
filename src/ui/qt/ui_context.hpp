// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What every page and the tray need from the application: the action runner and the per-user
// paths (the same ones the daemon uses, from the platform layer).
#pragma once
#include <string>

#include "config.hpp"
#include "platform/platform.hpp"
#include "ui/model/action_runner.hpp"

struct UiContext {
    spn::ui::ActionRunner *runner = nullptr;
    std::string configPath;   // <config dir>/dofscroll.conf
    std::string modesDir;     // <config dir>/modes
    std::string runtimeDir;   // status, log and mode request; may be empty
};

inline UiContext makeUiContext(spn::ui::ActionRunner *runner) {
    const spn::PlatformPaths p = spn::platformPaths();
    UiContext c;
    c.runner = runner;
    c.configPath = spn::defaultConfigPath();  // as the daemon: includes the legacy-path fallback
    c.modesDir = p.configDir + "/modes";
    c.runtimeDir = p.runtimeDir;
    return c;
}
