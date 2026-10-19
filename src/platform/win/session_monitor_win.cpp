// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows: active when this process's session is the active console session or its WTS connect
// state is WTSActive. A locked workstation is NOT detected (it would need a WTS_SESSION_LOCK window
// message); a locked session therefore still counts as active.
#include <windows.h>
#include <wtsapi32.h>

#include <string>

#include "../session_monitor.hpp"

namespace spn {
namespace {

class WtsMonitor : public SessionMonitor {
public:
    bool isActive(std::string *detail) override {
        DWORD own = 0;
        const int64_t ownId =
            ProcessIdToSessionId(GetCurrentProcessId(), &own) ? static_cast<int64_t>(own) : -1;
        const DWORD console = WTSGetActiveConsoleSessionId();
        int state = -1;
        if (ownId >= 0) {
            LPWSTR buf = nullptr;
            DWORD bytes = 0;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, own, WTSConnectState, &buf, &bytes) &&
                buf) {
                if (bytes >= sizeof(int)) state = *reinterpret_cast<int *>(buf);
                WTSFreeMemory(buf);
            }
        }
        const SessionState s = decideWindowsSession(ownId, static_cast<int64_t>(console), state);
        if (detail)
            *detail = "wts session " + std::to_string(ownId) + ", console " +
                      std::to_string(static_cast<int64_t>(console)) + ", state " + std::to_string(state);
        return s != SessionState::Inactive;
    }
};

}  // namespace

std::unique_ptr<SessionMonitor> makeSessionMonitor() { return std::make_unique<WtsMonitor>(); }

}  // namespace spn
