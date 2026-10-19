// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Shared X11 error handlers (process-wide, installed once).
// Used by output_x11.cpp and activewin.cpp; init order does not matter
// because the installer is idempotent.
#pragma once

#include <X11/Xlib.h>
#include <unistd.h>

#include <mutex>

namespace spn {

// Set by the X error handler in the calling thread (BadWindow on just-closed
// windows is normal). Callers reset it to 0 before the query.
inline thread_local int g_xerror = 0;

inline int xErrorHandler(Display *, XErrorEvent *) {
    g_xerror = 1;
    return 0;
}

// Xlib requires an IO error handler not to return (otherwise exit(1) with running
// threads/static destructors). So: short message via write(2) and an immediate
// _exit(1); ExecStopPost --release-keys releases keys afterwards.
inline int xIOErrorHandler(Display *) {
    static const char msg[] = "dofscroll: X11 connection lost, exiting\n";
    ssize_t r = write(2, msg, sizeof msg - 1);
    (void)r;
    _exit(1);
}

// Idempotent and thread-safe.
inline void installXErrorHandlers() {
    static std::once_flag once;
    std::call_once(once, [] {
        XSetErrorHandler(xErrorHandler);
        XSetIOErrorHandler(xIOErrorHandler);
    });
}

}  // namespace spn
