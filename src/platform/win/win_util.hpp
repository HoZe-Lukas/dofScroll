// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Small Windows string/environment helpers shared by win_platform.cpp (daemon) and client_win.cpp
// (daemon and UI). UTF-8 on our side, wide strings only at the Win32 boundary.
#pragma once
#ifndef _WIN32
#error "win_util.hpp is for Windows only"
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

namespace spn::winutil {

inline std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

inline std::string envVar(const wchar_t *name) {
    std::vector<wchar_t> buf(32768);
    const DWORD n = ::GetEnvironmentVariableW(name, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return "";
    return narrow(std::wstring(buf.data(), n));
}

inline std::string lastErrorText(DWORD code) {
    wchar_t *buf = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string s = n && buf ? narrow(std::wstring(buf, n)) : "error " + std::to_string(static_cast<unsigned long>(code));
    if (buf) ::LocalFree(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s;
}

}  // namespace spn::winutil
