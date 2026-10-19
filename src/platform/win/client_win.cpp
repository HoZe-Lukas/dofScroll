// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Client side of the Windows platform layer, linked by the daemon AND the UI (no input/output
// backends): per-user paths and the reload request (named event the daemon listens on).
#ifndef _WIN32
#error "client_win.cpp is built on Windows only"
#endif
#include <map>
#include <string>

#include "../platform.hpp"
#include "win_paths.hpp"
#include "win_util.hpp"

namespace spn {

PlatformPaths platformPaths() {
    std::map<std::string, std::string> env;
    env["APPDATA"] = winutil::envVar(L"APPDATA");
    env["LOCALAPPDATA"] = winutil::envVar(L"LOCALAPPDATA");
    env["USERPROFILE"] = winutil::envVar(L"USERPROFILE");
    const WinPaths wp = windowsPaths(env);
    PlatformPaths p;
    p.configDir = wp.configDir;
    p.runtimeDir = wp.runtimeDir;
    return p;
}

bool requestReload(std::string *err) {
    const std::wstring name = winutil::widen(reloadEventName());
    HANDLE ev = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!ev) {
        if (err) *err = "dofscroll is not running (event " + std::string(reloadEventName()) + "): " + winutil::lastErrorText(::GetLastError());
        return false;
    }
    const BOOL ok = ::SetEvent(ev);
    const DWORD e = ::GetLastError();
    ::CloseHandle(ev);
    if (!ok) {
        if (err) *err = "SetEvent: " + winutil::lastErrorText(e);
        return false;
    }
    return true;
}

}  // namespace spn
