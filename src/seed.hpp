// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First-start seeding: copy the example configuration (examples/default/dofscroll.conf) into the
// configuration directory if none exists there yet. The joystick modes and button modes are not
// copied: they are built into the program (builtin_library.hpp). Called by the tray at startup (the daemon has no write access to the
// configuration directory).
#pragma once
#include <string>

namespace spn {

// Returns true if something was created (*log = one-line description, may be nullptr).
// Never overwrites an existing file. See seed.cpp for the search paths.
bool seedDefaults(std::string *log);

// The examples directory the seeding uses ($DOFSCROLL_EXAMPLES, next to the executable, ...); "" if none.
std::string examplesDir();

}  // namespace spn
