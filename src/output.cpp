// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Backend selection for event output (x11 / uinput / auto).
#include "output.hpp"

#include <cstdlib>
#include <cstring>

namespace spn {

namespace {
std::unique_ptr<Output> tryBackend(const std::string &which, std::string *why) {
    std::unique_ptr<Output> o = (which == "uinput") ? makeUinputOutput() : makeX11Output();
    if (!o) {
        *why = which + ": backend not available";
        return nullptr;
    }
    std::string err;
    if (!o->init(&err)) {
        *why = which + ": " + (err.empty() ? "init failed" : err);
        return nullptr;
    }
    return o;
}
}  // namespace

std::unique_ptr<Output> makeOutput(const std::string &backend, std::string *log) {
    std::string dummy;
    if (!log) log = &dummy;
    log->clear();

    if (backend == "x11" || backend == "uinput") {
        std::string why;
        auto o = tryBackend(backend, &why);
        if (o) *log = "Backend " + backend + " (forced by configuration)";
        else *log = "Backend " + backend + " forced, but not usable: " + why;
        return o;
    }

    const char *st = std::getenv("XDG_SESSION_TYPE");
    const bool wayland = st && std::strcmp(st, "wayland") == 0;
    const std::string first = wayland ? "uinput" : "x11";
    const std::string second = wayland ? "x11" : "uinput";
    const std::string reason = wayland ? "XDG_SESSION_TYPE=wayland" : std::string("XDG_SESSION_TYPE=") + (st ? st : "(not set)");

    std::string why1;
    if (auto o = tryBackend(first, &why1)) {
        *log = "Backend " + first + " selected (auto, " + reason + ")";
        return o;
    }
    std::string why2;
    if (auto o = tryBackend(second, &why2)) {
        *log = "Backend " + second + " selected (auto, fallback, " + reason + "; " + why1 + ")";
        return o;
    }
    *log = "No output backend usable (auto, " + reason + "): " + why1 + "; " + why2;
    return nullptr;
}

}  // namespace spn
