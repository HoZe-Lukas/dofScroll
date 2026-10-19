// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux: the logind state file of this session (no libsystemd, no D-Bus). Without XDG_SESSION_ID
// (or an unreadable file) the session counts as active.
#include <cstdlib>

#include "../session_monitor.hpp"

namespace spn {

std::unique_ptr<SessionMonitor> makeSessionMonitor() {
    const char *id = std::getenv("XDG_SESSION_ID");
    return std::make_unique<LogindFileMonitor>("/run/systemd/sessions", id ? id : "");
}

}  // namespace spn
