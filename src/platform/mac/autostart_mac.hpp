// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS autostart on top of LaunchAgents in ~/Library/LaunchAgents. queryAutostart/setAutostart
// (see ../autostart_platform.hpp) only write or remove the plists: launchd loads every agent of
// that folder at the next login, and nothing that runs now (possibly the caller itself) is
// stopped. setAgents(..., applyNow=true) additionally does what packaging/macos/install-
// launchagent.sh does (bootout, wait until gone, bootstrap into gui/<uid>); it is meant for
// installers, not for a checkbox. The list-based functions take the folder so the ctest renders
// into a temporary directory.
#pragma once

#include <string>
#include <vector>

#include "../autostart_os.hpp"

namespace spn {
namespace autostart {

struct AgentSpec {
    std::string label;    // io.github.dofscroll.daemon / .ui
    std::string program;  // absolute path of the executable
    bool daemon = false;  // plist flavour (KeepAlive on crash) and ordering
    bool optional = false;
};

// Plist file of an agent: <dir>/<label>.plist
std::string agentPlistPath(const std::string &dir, const std::string &label);

AutostartState queryAgents(const std::string &dir, const std::vector<AgentSpec> &specs, std::string *detail);
// enabled: write plists (atomic, 0644); disabled: remove them. applyNow: also launchctl, see above.
bool setAgents(const std::string &dir, const std::vector<AgentSpec> &specs, bool enabled, bool applyNow,
               unsigned uid, std::string *err);

}  // namespace autostart
}  // namespace spn
