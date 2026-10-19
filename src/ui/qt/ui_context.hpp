// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What every page and the tray need from the application. The paths and the runner are set by
// main(); the shared models (settings, status, modes, strings) are owned by the MainWindow, which
// completes the context before it builds the pages. A page used standalone (the tray, tests) may
// get null model pointers and must then create or skip what it needs.
#pragma once
#include <functional>
#include <string>

#include "config.hpp"
#include "platform/platform.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"

struct UiContext {
    spn::ui::ActionRunner *runner = nullptr;
    std::string configPath;   // <config dir>/dofscroll.conf
    std::string modesDir;     // <config dir>/modes
    std::string runtimeDir;   // status, log and mode request; may be empty
    // Reloads the daemon's configuration after an atomic save; defaults to runner->requestReload.
    // Real channel per OS (src/platform/<os>/client_*.cpp): Linux `systemctl --user kill -s HUP
    // dofscroll.service`, Windows the named event Local\dofscroll-reload, macOS a connection to
    // <runtimeDir>/reload.sock. Pages save through ctx.settings (which calls it), never directly.
    std::function<bool(std::string *err)> reload;
    // Shared models, owned by the MainWindow (outlive the pages).
    spn::ui::SettingsModel *settings = nullptr;  // edit through it, then save()
    spn::ui::StatusModel *status = nullptr;      // daemon status, refreshed by the main window
    spn::ui::ModeCatalog *modes = nullptr;       // the modes folder
    const spn::ui::StringProvider *strings = nullptr;
};

inline UiContext makeUiContext(spn::ui::ActionRunner *runner) {
    const spn::PlatformPaths p = spn::platformPaths();
    UiContext c;
    c.runner = runner;
    c.configPath = spn::defaultConfigPath();  // as the daemon: includes the legacy-path fallback
    c.modesDir = p.configDir + "/modes";
    c.runtimeDir = p.runtimeDir;
    if (runner) c.reload = spn::ui::reloadVia(*runner);
    return c;
}
