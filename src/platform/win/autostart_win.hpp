// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows autostart on top of the HKCU Run key. queryAutostart/setAutostart (see
// ../autostart_platform.hpp) build the entry list from the folder of the running executable; the
// list-based functions are public so the ctest round trip can use a separate value name.
#pragma once

#include <string>
#include <vector>

#include "../autostart_os.hpp"

namespace spn {
namespace autostart {

struct RunEntry {
    std::string valueName;  // UTF-8, e.g. "dofscroll"
    std::string exePath;    // UTF-8 absolute path (quoted when written)
    bool optional = false;  // not installed -> ignored (the UI)
};

// Query of the given entries (a missing file on disk gives Entry::Missing).
AutostartState queryRunEntries(const std::vector<RunEntry> &entries, std::string *detail);
// Writes (enabled) or deletes the values. Deleting a value that does not exist is fine.
bool setRunEntries(const std::vector<RunEntry> &entries, bool enabled, std::string *err);

}  // namespace autostart
}  // namespace spn
