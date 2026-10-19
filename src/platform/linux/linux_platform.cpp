// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux implementations of the platform seams that need no libspnav: XDG paths, SIGHUP reload
// channel, X11 active window (wraps src/activewin).
#include <csignal>
#include <string>

#include "../../activewin.hpp"
#include "../platform.hpp"

namespace spn {

// platformPaths() and requestReload() (the client side) live in client_linux.cpp, which the UI links too.

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

std::unique_ptr<ReloadListener> makeReloadListener() {
    return std::make_unique<SignalReloadListener>();
}

std::unique_ptr<ActiveWindowProvider> makeActiveWindowProvider() {
    return std::make_unique<X11ActiveWindowProvider>();
}

}  // namespace spn
