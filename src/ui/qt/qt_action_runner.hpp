// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ActionRunner of the Qt interface: the real implementation of the platform actions the models
// need (open a file, start at login, ask the daemon to reload). openPath and setAutostart may be
// called from a worker thread.
#pragma once
#include <string>

#include "ui/model/action_runner.hpp"

class QtActionRunner : public spn::ui::ActionRunner {
public:
    // QDesktopServices: xdg-open, ShellExecute or open, whichever the platform uses.
    bool openPath(const std::string &path, std::string *err) override;
    // The platform autostart interface (src/platform/autostart_platform.hpp).
    bool setAutostart(bool enable, std::string *err) override;
    // Linux: `systemctl --user kill -s HUP dofscroll.service` as the GTK tray does. Windows and
    // macOS: the daemon notices a changed configuration file by itself, nothing to send here.
    bool requestReload(std::string *err) override;
};
