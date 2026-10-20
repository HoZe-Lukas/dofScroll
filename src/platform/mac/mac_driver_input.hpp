// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// InputSource that reads the SpaceMouse through the client interface of the installed 3Dconnexion
// driver on macOS (loaded at runtime, nothing bundled; see mac_client_api.hpp and docs/legal-notes.md).
// The source is POSIX code apart from the run-loop pump, so it also builds on Linux, where the tests
// run it against a fake driver library (tests/fake_driver/).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "../platform.hpp"

namespace spn {

// (vendor, product) -> model name and factory button count; false for an unknown model.
using DriverModelLookup = std::function<bool(unsigned vendor, unsigned product, std::string *name, int *buttons)>;

// True when the installed driver's client library exists at its fixed location, is inside the
// framework directory after resolving links, and belongs to root without group/other write access.
// `why` (may be null) receives the reason otherwise. The location is fixed: no environment variable or
// setting can change it.
bool macDriverInstalled(std::string *why);

// The production source: fixed library location (checked as above), device models from the
// devices/*.conf descriptions in `descriptorDir` (may be empty: every device is named
// "SpaceMouse (driver)"). Returns null when the driver is not installed.
std::unique_ptr<InputSource> makeMacDriverInput(const std::string &descriptorDir, std::string *log);

// For tests only: loads the library at `libraryPath` (no location or ownership checks). Never reachable
// from the daemon's configuration.
std::unique_ptr<InputSource> makeMacDriverInputFromPath(const std::string &libraryPath, DriverModelLookup lookup);

}  // namespace spn
