// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Win event output (declaration shared by the backend files; see docs/port-architecture.md).
#pragma once
#include <memory>
#include <string>

#include "../../output.hpp"

namespace spn {

// Makes the next outputs release every key in forceReleaseAll() (for --release-keys); by default
// forceReleaseAll() releases only what the process holds itself.
void setWinBlanketRelease(bool on);

// Creates the output (not yet init()ed). `log` may be null.
std::unique_ptr<Output> makeWinOutput(std::string *log);

}  // namespace spn
