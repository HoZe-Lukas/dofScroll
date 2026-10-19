// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Event output abstraction: X11/XTest or uinput (Wayland-capable).
#pragma once
#include <memory>
#include <string>

#include "keys.hpp"

namespace spn {

class Output {
public:
    virtual ~Output() = default;
    virtual const char *name() const = 0;           // "x11" or "uinput"
    virtual bool init(std::string *err) = 0;

    // n > 0: down resp. right, n < 0: up resp. left. |n| detent steps.
    virtual void wheelV(int n) = 0;
    virtual void wheelH(int n) = 0;
    // Hold/release (e.g. Ctrl for zoom). Idempotent.
    virtual void setKey(Key k, bool down) = 0;
    virtual void tapKey(Key k) = 0;                 // press and release (e.g. arrow keys in nav mode)
    virtual void mouseButton(int button, bool down) = 0;  // 1 = left, 2 = middle, 3 = right
    virtual void releaseAll() = 0;                  // release every held key/button
    // Like releaseAll(), but independent of our own state: releases every Key and mouse
    // buttons 1-3 even if an earlier process (crash, SIGKILL) left them pressed.
    // Default: releaseAll().
    virtual void forceReleaseAll() { releaseAll(); }
    virtual void flush() = 0;
};

// Factories (in output_x11.cpp / output_uinput.cpp respectively).
std::unique_ptr<Output> makeX11Output();
std::unique_ptr<Output> makeUinputOutput();

// "auto": XDG_SESSION_TYPE=wayland -> uinput, otherwise x11; falls back to the other
// one if init() fails. backend "x11"/"uinput" forces one. In output.cpp.
std::unique_ptr<Output> makeOutput(const std::string &backend, std::string *log);

}  // namespace spn
