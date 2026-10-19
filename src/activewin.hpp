// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Detection of the active program (WM_CLASS) for [app:*] profiles.
#pragma once
#include <string>

namespace spn {

class ActiveWindow {
public:
    ActiveWindow();
    ~ActiveWindow();
    ActiveWindow(const ActiveWindow &) = delete;
    ActiveWindow &operator=(const ActiveWindow &) = delete;
    // X11: _NET_ACTIVE_WINDOW + WM_CLASS via a dedicated display (with its own
    // error handler, must never crash). On pure Wayland without an alternative
    // returns false -> poll() then returns "". With optional KDE fallback.
    bool init();
    // "instance class" joined in lowercase, e.g. "navigator firefox";
    // empty if unknown. Returns only the cache of a background thread (all X11
    // round trips / kdotool calls run there) and never blocks the caller.
    // Precondition: XInitThreads() was called before the process's first Xlib call.
    std::string poll();
private:
    struct Impl;
    Impl *impl_;
};

}  // namespace spn
