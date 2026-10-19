// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Runs the OS running-programs provider on the CI runner (Windows/macOS only, see cmake/*.cmake).
// Asserts only: no crash, a list comes back, entries are well formed. An empty list (no desktop
// session) is accepted.
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "platform/running_apps.hpp"
#include "platform/running_apps_os.hpp"

int main() {
    std::unique_ptr<spn::RunningAppsProvider> p = spn::makeRunningAppsProvider();
    if (!p) {
        std::fprintf(stderr, "no provider\n");
        return 1;
    }
    std::vector<spn::RunningApp> apps;
    std::string err;
    if (!p->list(&apps, &err)) {
        std::fprintf(stderr, "list failed: %s\n", err.c_str());
        return 1;
    }
    std::set<std::string> seen;
    for (const spn::RunningApp &a : apps) {
        if (a.matchString.empty() || spn::isOwnAppString(a.matchString) || !seen.insert(a.matchString).second) {
            std::fprintf(stderr, "bad entry '%s'\n", a.matchString.c_str());
            return 1;
        }
    }
    std::printf("running apps: %zu entries%s\n", apps.size(), apps.empty() ? " (no desktop session?)" : "");
    for (const spn::RunningApp &a : apps)
        std::printf("  %s | %s | %s\n", a.matchString.c_str(), a.displayName.c_str(), a.windowTitle.c_str());
    return 0;
}
