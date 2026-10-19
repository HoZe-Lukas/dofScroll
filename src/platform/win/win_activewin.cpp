// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#ifndef _WIN32
#error "win_activewin.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "win_activewin.hpp"
#include "win_paths.hpp"

namespace spn {

namespace {

std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// ---- active window: foreground window -> executable name -----------------------------------------

class ForegroundWindowProvider : public ActiveWindowProvider {
public:
    bool init() override { return true; }
    std::string poll() override {
        HWND hw = ::GetForegroundWindow();
        if (!hw) return "";  // no foreground window (lock screen, desktop switch)
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hw, &pid);
        if (!pid) return "";
        if (pid == cached_pid_ && ::GetTickCount64() - cached_at_ < 1000) return cached_;
        std::string name = exeOfProcess(pid);
        cached_pid_ = pid;
        cached_at_ = ::GetTickCount64();
        cached_ = name;
        return name;
    }

private:
    static std::string exeOfProcess(DWORD pid) {
        // PROCESS_QUERY_LIMITED_INFORMATION also works for most processes of other users and for
        // protected ones; elevated/system processes can still deny it -> empty = default profile.
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return "";
        std::vector<wchar_t> buf(32768);
        DWORD n = static_cast<DWORD>(buf.size());
        std::string out;
        if (::QueryFullProcessImageNameW(h, 0, buf.data(), &n) && n > 0)
            out = exeNameLower(narrow(std::wstring(buf.data(), n)));
        ::CloseHandle(h);
        // UWP apps run inside ApplicationFrameHost.exe (the frame) - the real app is a child
        // window's process; resolving it is not implemented, so the default profile applies.
        if (isUwpHostExe(out)) return "";
        return out;
    }

    DWORD cached_pid_ = 0;
    ULONGLONG cached_at_ = 0;
    std::string cached_;
};

}  // namespace

std::unique_ptr<ActiveWindowProvider> makeWinForegroundProvider() {
    return std::make_unique<ForegroundWindowProvider>();
}

}  // namespace spn
