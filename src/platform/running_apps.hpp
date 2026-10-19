// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// List of currently running programs for the "Add application" dialog (toolkit-free).
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace spn {

struct RunningApp {
    // The string an [app:*] profile is matched against. MUST be byte-identical to what the
    // ActiveWindowProvider of the same OS returns while this program has the focus:
    //   Linux/X11: WM_CLASS as "instance class", lowercase (see platform/running_apps_util.hpp)
    //   Windows:   executable file name (same rule as the foreground provider)
    //   macOS:     bundle identifier, or the app name when there is none (as the foreground provider)
    std::string matchString;
    std::string displayName;  // human readable name (X11: WM_CLASS class part, original case)
    std::string windowTitle;  // title of the most recently listed window of this program
    std::string iconHint;     // icon theme name or empty
    int pid = 0;              // 0 if unknown
    int windowCount = 1;      // number of top-level windows folded into this entry
};

class RunningAppsProvider {
public:
    virtual ~RunningAppsProvider() = default;
    // Fills *out (one entry per distinct matchString). Returns false and sets *err (if non-null)
    // when listing is impossible (e.g. Wayland session); *out is then empty.
    virtual bool list(std::vector<RunningApp> *out, std::string *err) = 0;
};

// Linux: implemented in platform/linux/running_apps_x11.cpp (needs Xlib; it is linked into the
// daemon through the src/platform/linux glob, a future UI binary must add that file and X11).
// Windows/macOS: to be added in the OS port branches (see docs/port-architecture.md).
std::unique_ptr<RunningAppsProvider> makeRunningAppsProvider();

}  // namespace spn
