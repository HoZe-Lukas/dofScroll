// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Directory of the live feed socket (the pipe name on Windows carries the user SID, no directory).
#pragma once
#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include "platform/mac/mac_paths.hpp"
#endif

namespace dofscroll_ui {

inline std::string feedRuntimeDir() {
#if defined(_WIN32)
    return std::string();
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    return spn::macPaths(home ? home : "").runtimeDir;
#else
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    return rt && *rt ? rt : "";
#endif
}

}  // namespace dofscroll_ui
