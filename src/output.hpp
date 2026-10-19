// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Abstraktion der Ereignis-Ausgabe: X11/XTest oder uinput (Wayland-tauglich).
#pragma once
#include <memory>
#include <string>

#include "keys.hpp"

namespace spn {

class Output {
public:
    virtual ~Output() = default;
    virtual const char *name() const = 0;           // "x11" oder "uinput"
    virtual bool init(std::string *err) = 0;

    // n > 0: runter bzw. rechts, n < 0: hoch bzw. links. |n| Rasterschritte.
    virtual void wheelV(int n) = 0;
    virtual void wheelH(int n) = 0;
    // Halten/Loslassen (Strg fuer Zoom). Idempotent.
    virtual void setKey(Key k, bool down) = 0;
    virtual void tapKey(Key k) = 0;                 // Pfeiltasten im Nav-Modus
    virtual void mouseButton(int button, bool down) = 0;  // 1 = links, 2 = Mitte, 3 = rechts
    virtual void releaseAll() = 0;                  // alle gehaltenen Tasten/Buttons frei
    // Wie releaseAll(), aber unabhaengig vom eigenen Zustand: gibt Strg, Pfeiltasten
    // und linke Maustaste auch dann frei, wenn ein frueherer Prozess (Absturz, SIGKILL)
    // sie gedrueckt zurueckgelassen hat. Standard: releaseAll().
    virtual void forceReleaseAll() { releaseAll(); }
    virtual void flush() = 0;
};

// Factories (je in output_x11.cpp / output_uinput.cpp).
std::unique_ptr<Output> makeX11Output();
std::unique_ptr<Output> makeUinputOutput();

// "auto": XDG_SESSION_TYPE=wayland -> uinput, sonst x11; Fallback auf das jeweils
// andere, wenn init() scheitert. backend "x11"/"uinput" erzwingt. In output.cpp.
std::unique_ptr<Output> makeOutput(const std::string &backend, std::string *log);

}  // namespace spn
