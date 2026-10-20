// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "platform_hooks.hpp"

#include <cstdio>
#include <memory>
#include <vector>

#include "platform/platform.hpp"
#include "platform/running_apps.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(DOFSCROLL_UI_X11)
#include <X11/Xlib.h>

#include "activewin.hpp"
#elif defined(DOFSCROLL_UI_MAC_AW)
#include "platform/mac/mac_activewin.hpp"
#elif defined(DOFSCROLL_UI_WIN_AW)
#include "platform/win/win_activewin.hpp"
#endif

#if defined(DOFSCROLL_UI_RUNNING_APPS_STUB)
namespace spn {
namespace {
class NoRunningApps : public RunningAppsProvider {
public:
    bool list(std::vector<RunningApp> *out, std::string *err) override {
        out->clear();
        if (err) *err = "listing running programs is not available in this build";
        return false;
    }
};
}  // namespace
std::unique_ptr<RunningAppsProvider> makeRunningAppsProvider() {
    return std::unique_ptr<RunningAppsProvider>(new NoRunningApps);
}
}  // namespace spn
#endif

namespace dofscroll_ui {

void earlyPlatformInit() {
#if defined(DOFSCROLL_UI_X11)
    // ActiveWindow polls from a background thread, so Xlib must be thread safe. XInitThreads() has
    // to be the first Xlib call of the process: before QApplication, whose platform plugin (or an
    // OpenGL integration) may use Xlib too. It does not connect to the display, so it is harmless
    // under Wayland.
    static const int threadsInit = XInitThreads();
    (void)threadsInit;
#endif
}

void attachParentConsole() {
#if defined(_WIN32)
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;  // no parent console (double click, service)
    auto unset = [](DWORD which) {
        const HANDLE h = GetStdHandle(which);
        return h == nullptr || h == INVALID_HANDLE_VALUE;
    };
    // A stream that is redirected to a file or pipe (the caller captures it) stays as it is.
    if (unset(STD_OUTPUT_HANDLE) && std::freopen("CONOUT$", "w", stdout)) std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (unset(STD_ERROR_HANDLE) && std::freopen("CONOUT$", "w", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
}

std::function<std::string()> makeActiveWindowPoll() {
#if defined(DOFSCROLL_UI_X11)
    // XInitThreads() already ran in earlyPlatformInit(), before QApplication.
    auto win = std::make_shared<spn::ActiveWindow>();
    if (!win->init()) return nullptr;  // e.g. a Wayland session without a fallback
    return [win] { return win->poll(); };
#elif defined(DOFSCROLL_UI_MAC_AW)
    std::shared_ptr<spn::ActiveWindowProvider> p = spn::makeMacActiveWindowProvider();
    if (!p || !p->init()) return nullptr;
    return [p] { return p->poll(); };
#elif defined(DOFSCROLL_UI_WIN_AW)
    // Same string as the daemon uses for [app:*] matching: the lowercased executable name.
    std::shared_ptr<spn::ActiveWindowProvider> p = spn::makeWinForegroundProvider();
    if (!p || !p->init()) return nullptr;
    return [p] { return p->poll(); };
#else
    return nullptr;
#endif
}

}  // namespace dofscroll_ui
