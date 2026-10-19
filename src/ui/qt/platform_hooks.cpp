// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "platform_hooks.hpp"

#include <memory>
#include <vector>

#include "platform/platform.hpp"
#include "platform/running_apps.hpp"

#if defined(DOFSCROLL_UI_X11)
#include <X11/Xlib.h>

#include "activewin.hpp"
#elif defined(DOFSCROLL_UI_MAC_AW)
#include "platform/mac/mac_activewin.hpp"
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

std::function<std::string()> makeActiveWindowPoll() {
#if defined(DOFSCROLL_UI_X11)
    // ActiveWindow needs XInitThreads() before the process's first Xlib call (it polls from a
    // background thread). Qt's xcb plugin does not use Xlib, so this is still the first call.
    static const int threadsInit = XInitThreads();
    (void)threadsInit;
    auto win = std::make_shared<spn::ActiveWindow>();
    if (!win->init()) return nullptr;  // e.g. a Wayland session without a fallback
    return [win] { return win->poll(); };
#elif defined(DOFSCROLL_UI_MAC_AW)
    std::shared_ptr<spn::ActiveWindowProvider> p = spn::makeMacActiveWindowProvider();
    if (!p || !p->init()) return nullptr;
    return [p] { return p->poll(); };
#else
    return nullptr;  // Windows: the foreground provider is part of the daemon target; not wired yet
#endif
}

}  // namespace dofscroll_ui
