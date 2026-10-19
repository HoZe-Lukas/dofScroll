// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Gemeinsame X11-Fehlerhandler (prozessweit, einmal installiert).
// Wird von output_x11.cpp und activewin.cpp genutzt; die Init-Reihenfolge ist egal,
// da der Installer idempotent ist.
#pragma once

#include <X11/Xlib.h>
#include <unistd.h>

#include <mutex>

namespace spn {

// Wird vom X-Fehlerhandler im aufrufenden Thread gesetzt (BadWindow auf gerade
// geschlossenen Fenstern ist normal). Aufrufer setzen es vor der Abfrage auf 0.
inline thread_local int g_xerror = 0;

inline int xErrorHandler(Display *, XErrorEvent *) {
    g_xerror = 1;
    return 0;
}

// Xlib verlangt, dass ein IO-Error-Handler nicht zurueckkehrt (sonst exit(1) mit
// laufenden Threads/statischen Destruktoren). Daher: kurze Meldung per write(2)
// und sofort _exit(1); ExecStopPost --release-keys gibt Tasten danach frei.
inline int xIOErrorHandler(Display *) {
    static const char msg[] = "dofscroll: X11-Verbindung verloren, beende\n";
    ssize_t r = write(2, msg, sizeof msg - 1);
    (void)r;
    _exit(1);
}

// Idempotent und thread-sicher.
inline void installXErrorHandlers() {
    static std::once_flag once;
    std::call_once(once, [] {
        XSetErrorHandler(xErrorHandler);
        XSetIOErrorHandler(xIOErrorHandler);
    });
}

}  // namespace spn
