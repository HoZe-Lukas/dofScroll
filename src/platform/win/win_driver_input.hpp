// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// InputSource that reads the SpaceMouse through the COM interface of the installed 3Dconnexion driver
// on Windows (late binding through IDispatch, nothing bundled; see win_driver_api.hpp and
// docs/legal-notes.md).
#pragma once
#include <memory>
#include <string>

#include "../platform.hpp"

namespace spn {

// True when the driver's COM object is registered in the machine-wide registry and its server file
// lies under Program Files (or, for a managed server, its code base does and the host is a system
// DLL) and the current user has no override of the registration. `why` (may be null) gets the reason
// otherwise. The ProgID is fixed (kWinDriverProgId): no setting can change it.
bool winDriverInstalled(std::string *why);

// The production source (checks as above; re-checked at every open()). Null when the driver is not
// installed; `log` (may be null) then receives the reason.
std::unique_ptr<InputSource> makeWinDriverInput(std::string *log);

// For tests only: a source for a COM class registered under `progId` (the test registers a fake in
// HKCU). No location checks. Never reachable from the daemon's configuration.
std::unique_ptr<InputSource> makeWinDriverInputForProgId(const std::wstring &progId);

}  // namespace spn
