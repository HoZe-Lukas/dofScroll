// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux implementations of the platform seams that need no libspnav: XDG paths, SIGHUP reload
// channel, X11 active window (wraps src/activewin).
#include <csignal>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "../../activewin.hpp"
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

namespace {

volatile std::sig_atomic_t g_reload_flag = 0;
void onSighup(int) { g_reload_flag = 1; }

class SignalReloadListener : public ReloadListener {
public:
    bool init(std::string *err) override {
        struct sigaction sa {};
        sa.sa_handler = onSighup;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGHUP, &sa, nullptr) != 0) {
            if (err) *err = "sigaction(SIGHUP) failed";
            return false;
        }
        return true;
    }
    bool pollReload() override {
        if (!g_reload_flag) return false;
        g_reload_flag = 0;
        return true;
    }
};

class X11ActiveWindowProvider : public ActiveWindowProvider {
public:
    bool init() override { return win_.init(); }
    std::string poll() override { return win_.poll(); }
private:
    ActiveWindow win_;
};

}  // namespace

// Same mechanism the tray uses today: ask the user's systemd instance to send SIGHUP to the
// daemon unit.
bool requestReload(std::string *err) {
    pid_t pid = ::fork();
    if (pid < 0) {
        if (err) *err = "fork failed";
        return false;
    }
    if (pid == 0) {
        ::execlp("systemctl", "systemctl", "--user", "kill", "-s", "HUP", "dofscroll.service",
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

std::unique_ptr<ReloadListener> makeReloadListener() {
    return std::make_unique<SignalReloadListener>();
}

std::unique_ptr<ActiveWindowProvider> makeActiveWindowProvider() {
    return std::make_unique<X11ActiveWindowProvider>();
}

}  // namespace spn
