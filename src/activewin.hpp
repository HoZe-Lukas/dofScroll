// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Erkennung des aktiven Programms (WM_CLASS) fuer [app:*]-Profile.
#pragma once
#include <string>

namespace spn {

class ActiveWindow {
public:
    ActiveWindow();
    ~ActiveWindow();
    ActiveWindow(const ActiveWindow &) = delete;
    ActiveWindow &operator=(const ActiveWindow &) = delete;
    // X11: _NET_ACTIVE_WINDOW + WM_CLASS ueber ein eigenes Display (mit eigenem
    // Fehlerhandler, darf nie abstuerzen). Auf reinem Wayland ohne Moeglichkeit
    // false -> poll() liefert dann "". Mit optionalem KDE-Fallback.
    bool init();
    // "instance class" kleingeschrieben zusammengesetzt, z.B. "navigator firefox";
    // leer wenn unbekannt. Liefert nur den Zwischenspeicher eines Hintergrund-Threads
    // (alle X11-Roundtrips/kdotool-Aufrufe laufen dort) und blockiert den Aufrufer nie.
    // Voraussetzung: XInitThreads() wurde vor dem ersten Xlib-Aufruf des Prozesses gerufen.
    std::string poll();
private:
    struct Impl;
    Impl *impl_;
};

}  // namespace spn
