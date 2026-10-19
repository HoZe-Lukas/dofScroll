// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Running programs on Windows (see platform/running_apps.hpp): EnumWindows over visible top-level
// windows. matchString = executable file name, via the same rule as the foreground provider in
// win_platform.cpp (winMatchString -> exeNameLower, UWP frame host excluded). Compile-checked on
// the Windows runner; behaviour on a real desktop is unverified.
#ifndef _WIN32
#error "running_apps_win.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "../running_apps.hpp"
#include "../running_apps_os.hpp"

namespace spn {

namespace {

constexpr size_t kMaxWindows = 4096;

std::string narrow(const wchar_t *w, size_t n) {
    if (n == 0) return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), &s[0], len, nullptr, nullptr);
    return s;
}

// Full image path of a process; empty when it cannot be queried (access denied etc.).
std::wstring imagePathOf(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::wstring();
    std::vector<wchar_t> buf(32768);
    DWORD n = static_cast<DWORD>(buf.size());
    std::wstring out;
    if (::QueryFullProcessImageNameW(h, 0, buf.data(), &n) && n > 0) out.assign(buf.data(), n);
    ::CloseHandle(h);
    return out;
}

// FileDescription of the version resource ("" if none).
std::string fileDescription(const std::wstring &path) {
    DWORD dummy = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (size == 0 || size > (1u << 20)) return std::string();
    std::vector<char> data(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return std::string();
    struct LangCp {
        WORD lang, cp;
    } *tr = nullptr;
    UINT trLen = 0;
    if (!::VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&tr), &trLen) ||
        !tr || trLen < sizeof(LangCp))
        return std::string();
    wchar_t key[80];
    std::swprintf(key, sizeof key / sizeof key[0], L"\\StringFileInfo\\%04x%04x\\FileDescription",
                  static_cast<unsigned>(tr->lang), static_cast<unsigned>(tr->cp));
    wchar_t *val = nullptr;
    UINT valLen = 0;
    if (!::VerQueryValueW(data.data(), key, reinterpret_cast<void **>(&val), &valLen) || !val || valLen == 0)
        return std::string();
    size_t n = 0;
    while (n < valLen && val[n]) n++;  // valLen counts characters; never read past it
    return narrow(val, n);
}

struct EnumCtx {
    std::vector<RunningApp> wins;
    DWORD ownPid = 0;
};

BOOL CALLBACK enumProc(HWND hw, LPARAM lp) {
    EnumCtx *ctx = reinterpret_cast<EnumCtx *>(lp);
    if (ctx->wins.size() >= kMaxWindows) return FALSE;

    WinWindowFlags f;
    f.visible = ::IsWindowVisible(hw) != FALSE;
    const LONG_PTR ex = ::GetWindowLongPtrW(hw, GWL_EXSTYLE);
    f.toolWindow = (ex & WS_EX_TOOLWINDOW) != 0 && (ex & WS_EX_APPWINDOW) == 0;
    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(hw, DWMWA_CLOAKED, &cloaked, sizeof cloaked))) f.cloaked = cloaked != 0;
    wchar_t title[512];
    const int tl = ::GetWindowTextW(hw, title, static_cast<int>(sizeof title / sizeof title[0]));
    f.hasTitle = tl > 0;
    // Owned windows (dialogs) belong to the program of their owner, which is listed itself.
    if (!winWindowListable(f) || ::GetWindow(hw, GW_OWNER) != nullptr) return TRUE;

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hw, &pid);
    if (!pid || pid == ctx->ownPid) return TRUE;
    const std::wstring path = imagePathOf(pid);
    if (path.empty()) return TRUE;  // access denied (elevated/system process): skip, never fail
    const std::string utf8Path = narrow(path.data(), path.size());
    RunningApp a;
    a.matchString = winMatchString(utf8Path);
    if (a.matchString.empty()) return TRUE;
    a.displayName = winDisplayName(fileDescription(path), utf8Path);
    a.windowTitle = truncateUtf8(narrow(title, static_cast<size_t>(tl)));
    a.pid = static_cast<int>(pid);
    ctx->wins.push_back(std::move(a));
    return TRUE;
}

class WinRunningApps : public RunningAppsProvider {
public:
    // Callable from any thread: EnumWindows needs no message loop. Only windows of the current
    // desktop/window station are visible; without an interactive desktop (service, headless CI
    // session) the list is simply empty and true is returned.
    bool list(std::vector<RunningApp> *out, std::string *err) override {
        (void)err;
        if (out) out->clear();
        EnumCtx ctx;
        ctx.ownPid = ::GetCurrentProcessId();
        ::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
        // EnumWindows walks top to bottom in Z order; the dedupe contract wants oldest first.
        std::vector<RunningApp> rev(ctx.wins.rbegin(), ctx.wins.rend());
        if (out) *out = dedupeApps(rev);
        return true;
    }
};

}  // namespace

std::unique_ptr<RunningAppsProvider> makeRunningAppsProvider() {
    return std::make_unique<WinRunningApps>();
}

}  // namespace spn
