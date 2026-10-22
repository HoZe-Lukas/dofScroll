// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Client side of the Linux platform layer, linked by the daemon AND the UI (no libspnav, no X11):
// XDG paths and the reload request (the user's systemd instance sends SIGHUP to the daemon unit).
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "../exec_lookup.hpp"
#include "../../modes.hpp"  // modesDir(): "<config base>/dofscroll/modes"
#include "../platform.hpp"

namespace spn {

PlatformPaths platformPaths() {
    PlatformPaths p;
    // modesDir() resolves $XDG_CONFIG_HOME (or ~/.config) + "/dofscroll/modes"; its parent is
    // the config directory. Reusing it keeps one implementation of the XDG rules.
    const std::string m = modesDir();
    const size_t slash = m.rfind('/');
    p.configDir = slash == std::string::npos ? "." : m.substr(0, slash);
    // Only the private per-user runtime dir is used. Empty when XDG_RUNTIME_DIR is unset: a
    // shared fallback such as /tmp would not be safe for the status/suspend/log files, so the
    // features that need it stay off (same behavior as before the platform split).
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    p.runtimeDir = rt && *rt ? rt : "";
    return p;
}

// Same mechanism the tray uses today: ask the user's systemd instance to send SIGHUP to the
// daemon unit.
bool requestReload(std::string *err) {
    // systemctl from the fixed system directories, never through PATH (exec_lookup.hpp)
    const std::string systemctl = findHelper("systemctl");
    if (systemctl.empty()) {
        if (err) *err = "systemctl not found in /usr/bin, /usr/local/bin or /bin";
        return false;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        if (err) *err = "fork failed";
        return false;
    }
    if (pid == 0) {
        ::execl(systemctl.c_str(), "systemctl", "--user", "kill", "-s", "HUP", "dofscroll.service",
                 static_cast<char *>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (err) *err = "systemctl --user kill -s HUP dofscroll.service failed";
        return false;
    }
    return true;
}

}  // namespace spn
