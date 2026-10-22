// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Command line of the tray's "Release all keys": `dofscroll --release-keys`, a separate process
// that releases Ctrl, arrow keys and mouse buttons through its own output connection (independent
// of the running daemon). The program is the daemon binary next to the UI
// executable when it exists, otherwise plain "dofscroll" (looked up on PATH). argv only, no shell.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "platform/autostart_os.hpp"

namespace spn::ui {

inline const char *daemonExecutableName() {
#ifdef _WIN32
    return "dofscroll.exe";
#else
    return "dofscroll";
#endif
}

// exeDir: directory of the UI executable ('/' separators, may be empty); isExecutable(path) is
// the file test (injected for tests). The daemon next to the UI executable, else (macOS app) in the
// bundle's Resources/daemon, else the bare name.
inline std::vector<std::string> releaseKeysCommand(const std::string &exeDir,
                                                   const std::function<bool(const std::string &)> &isExecutable) {
    std::string program = daemonExecutableName();
    if (!exeDir.empty()) {
        const std::string cand = exeDir + (exeDir.back() == '/' ? "" : "/") + daemonExecutableName();
        const std::string bundle = spn::autostart::macBundleDaemonPath(exeDir);
        if (isExecutable && isExecutable(cand)) program = cand;
        else if (isExecutable && !bundle.empty() && isExecutable(bundle)) program = bundle;
    }
    return {program, "--release-keys"};
}

}  // namespace spn::ui
