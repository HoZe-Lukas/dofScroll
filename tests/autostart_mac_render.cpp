// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS only: renders the LaunchAgent plists into a temporary directory and queries them. Nothing
// is loaded into launchd and ~/Library/LaunchAgents is not touched.
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "platform/mac/autostart_mac.hpp"
#include "testutil.hpp"

using namespace spn::autostart;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void touch(const std::string &p) { std::ofstream(p) << "#!/bin/sh\n"; }

int main() {
    const std::string root = tu::makeTempDir("dofscroll-agents");
    const std::string bin = root + "/App & Co <x>";
    tu::stdfs::create_directories(bin);
    touch(bin + "/dofscroll");
    touch(bin + "/dofscroll-ui");
    const std::string agents = root + "/LaunchAgents";  // does not exist yet
    const std::vector<AgentSpec> specs = {{kMacDaemonLabel, bin + "/dofscroll", true, false},
                                          {kMacUiLabel, bin + "/dofscroll-ui", false, true}};
    std::string detail, err;
    CHECK(queryAgents(agents, specs, &detail) == AutostartState::Disabled);
    CHECK(setAgents(agents, specs, true, false, 0, &err));
    CHECK(queryAgents(agents, specs, &detail) == AutostartState::Enabled);
    struct stat st;
    CHECK(stat(agentPlistPath(agents, kMacDaemonLabel).c_str(), &st) == 0 && (st.st_mode & 0777) == 0644);
    CHECK(stat(agents.c_str(), &st) == 0 && (st.st_mode & 0777) == 0755);
    std::ifstream in(agentPlistPath(agents, kMacUiLabel));
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(ss.str().find("App &amp; Co &lt;x&gt;/dofscroll-ui") != std::string::npos);

    // a plist that points elsewhere is partial; enabling repairs it
    std::vector<AgentSpec> moved = specs;
    touch(bin + "/other");
    moved[1].program = bin + "/other";
    CHECK(queryAgents(agents, moved, &detail) == AutostartState::Partial);

    // UI not installed: only the daemon counts
    tu::stdfs::remove(bin + "/dofscroll-ui");
    CHECK(queryAgents(agents, specs, &detail) == AutostartState::Enabled);
    CHECK(setAgents(agents, specs, false, false, 0, &err));
    CHECK(queryAgents(agents, specs, &detail) == AutostartState::Disabled);
    CHECK(!tu::exists(agentPlistPath(agents, kMacDaemonLabel)) && !tu::exists(agentPlistPath(agents, kMacUiLabel)));

    tu::removeAll(root);
    if (failures == 0) std::printf("autostart_mac_render: ok\n");
    return failures == 0 ? 0 : 1;
}
