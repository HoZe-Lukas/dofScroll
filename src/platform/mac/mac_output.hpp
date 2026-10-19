// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Mac event output (declaration shared by the backend files; see docs/port-architecture.md).
#pragma once
#include <memory>
#include <string>

#include "../../output.hpp"
#include "mac_release_guard.hpp"

namespace spn {

// Creates the output (not yet init()ed). `log` may be null.
std::unique_ptr<Output> makeMacOutput(std::string *log);

// Registry of all live outputs of the process (leaked on purpose: usable from atexit handlers).
mac::ReleaseGuard &macReleaseGuard();

}  // namespace spn
