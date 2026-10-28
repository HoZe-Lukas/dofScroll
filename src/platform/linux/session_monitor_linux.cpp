// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux: the logind state file of this session (no libsystemd, no D-Bus). Without XDG_SESSION_ID
// the state of the user (/run/systemd/users/<uid>) decides; when that is unknown too, the session
// counts as active and a warning is logged once.
#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "../session_monitor.hpp"

namespace spn {

namespace {
class LinuxSessionMonitor : public LogindFileMonitor {
public:
    explicit LinuxSessionMonitor(std::string id)
        : LogindFileMonitor("/run/systemd/sessions", std::move(id), "/run/systemd/users",
                            static_cast<uint32_t>(::getuid())) {}
    bool isActive(std::string *detail) override {
        const bool a = LogindFileMonitor::isActive(detail);
        if (takeUnknownWarning())
            std::cerr << "[dofscroll] warning: XDG_SESSION_ID is not set and logind has no state for this user: "
                         "cannot tell when this session is in the background. With BACKEND=uinput the "
                         "daemon would keep injecting after a fast user switch; start it from the user "
                         "session (systemd --user) or set XDG_SESSION_ID."
                      << std::endl;
        return a;
    }
};
}  // namespace

std::unique_ptr<SessionMonitor> makeSessionMonitor() {
    const char *id = std::getenv("XDG_SESSION_ID");
    return std::make_unique<LinuxSessionMonitor>(id ? id : "");
}

}  // namespace spn
