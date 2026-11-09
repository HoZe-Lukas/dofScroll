// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Driver restart on Windows (platform/driver_restart_os.hpp): the processes of this user's session,
// WM_CLOSE for windows, TerminateProcess, CreateProcessW. Run on a real machine with 3DxWare 10.9
// (3DxService.exe -quiet, 3DxProfileServer.exe, 3DxSmartUi.exe).
#ifndef _WIN32
#error "driver_restart_win.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>

#include <string>
#include <vector>

#include "../driver_restart_os.hpp"
#include "../fs.hpp"

namespace spn::ui {

namespace {

std::string narrow(const wchar_t *w, size_t n) {
    if (n == 0) return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len > 0 ? len : 0), L'\0');
    if (len > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], len);
    return w;
}

std::string imagePathOf(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::string();
    std::vector<wchar_t> buf(32768);
    DWORD n = static_cast<DWORD>(buf.size());
    std::string out;
    if (::QueryFullProcessImageNameW(h, 0, buf.data(), &n) && n > 0) out = narrow(buf.data(), n);
    ::CloseHandle(h);
    return out;
}

struct CloseCtx {
    DWORD pid;
};

BOOL CALLBACK closeWindow(HWND hwnd, LPARAM lp) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == reinterpret_cast<CloseCtx *>(lp)->pid) ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

class WinOps : public DriverRestartOps {
public:
    bool list(std::vector<DriverProcess> *out, std::string *err) override {
        out->clear();
        DWORD mySession = 0;
        if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &mySession)) {
            *err = "the session cannot be determined";
            return false;
        }
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            *err = "the process list cannot be read";
            return false;
        }
        PROCESSENTRY32W e;
        e.dwSize = sizeof(e);
        for (BOOL ok = ::Process32FirstW(snap, &e); ok; ok = ::Process32NextW(snap, &e)) {
            DWORD session = 0;
            // only this user's session: the driver's system part runs in session 0/1 and is none of our business
            if (!::ProcessIdToSessionId(e.th32ProcessID, &session) || session != mySession) continue;
            const std::string path = imagePathOf(e.th32ProcessID);
            if (!path.empty()) out->push_back({static_cast<int>(e.th32ProcessID), path});
        }
        ::CloseHandle(snap);
        return true;
    }
    void askToClose(int pid) override {
        CloseCtx c{static_cast<DWORD>(pid)};
        ::EnumWindows(closeWindow, reinterpret_cast<LPARAM>(&c));
    }
    bool end(int pid, std::string *err) override {
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (!h) {
            const DWORD e = ::GetLastError();
            *err = e == ERROR_ACCESS_DENIED ? "access denied (administrator rights needed)"
                                            : e == ERROR_INVALID_PARAMETER ? "already gone" : "error " + std::to_string(e);
            return e == ERROR_INVALID_PARAMETER;  // gone by itself counts as ended
        }
        bool ok = ::TerminateProcess(h, 0) != 0;
        if (!ok) *err = "error " + std::to_string(::GetLastError());
        else if (::WaitForSingleObject(h, 4000) != WAIT_OBJECT_0) {
            ok = false;
            *err = "did not end in time";
        }
        ::CloseHandle(h);
        return ok;
    }
    bool start(const std::string &exe, const std::vector<std::string> &args, std::string *err) override {
        std::wstring cmd = L"\"" + widen(exe) + L"\"";
        for (const std::string &a : args) cmd += L" " + widen(a);  // the arguments are fixed words of the rules, never user input
        std::wstring dir = widen(exe);
        const size_t s = dir.find_last_of(L"\\/");
        dir = s == std::wstring::npos ? std::wstring() : dir.substr(0, s);
        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        if (!::CreateProcessW(widen(exe).c_str(), buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                              dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
            *err = "error " + std::to_string(::GetLastError());
            return false;
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return true;
    }
    void wait(int ms) override { ::Sleep(static_cast<DWORD>(ms)); }
};

}  // namespace

std::unique_ptr<DriverRestartOps> makeDriverRestartOps() { return std::unique_ptr<DriverRestartOps>(new WinOps); }

DriverRestartRules osDriverRestartRules() {
    // ProgramW6432 is the 64-bit folder even for a 32-bit program, ProgramFiles(x86) the 32-bit one
    std::string pf = fs::getEnvUtf8("ProgramW6432");
    if (pf.empty()) pf = fs::getEnvUtf8("ProgramFiles");
    return driverRestartRules(DriverOs::Windows, pf, fs::getEnvUtf8("ProgramFiles(x86)"), "");
}

}  // namespace spn::ui
