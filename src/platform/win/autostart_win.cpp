// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows "Start at login": values `dofscroll` and `dofscroll-ui` under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, each the quoted full path of the program.
// Registry API only (UTF-16), no shell or script, no administrator rights. The programs are
// looked up next to the running executable (GetModuleFileNameW).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "autostart_win.hpp"

namespace spn {
namespace autostart {

namespace {

const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring toWide(const std::string &s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string toUtf8(const std::wstring &w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string winError(LONG code) {
    return "Windows error " + std::to_string(static_cast<long>(code));
}

bool fileExists(const std::string &utf8) {
    const DWORD a = GetFileAttributesW(toWide(utf8).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Folder of the running executable with a trailing backslash ("" on failure).
std::string exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
        if (n == 0) return "";
        if (n < buf.size()) { buf.resize(n); break; }
        if (buf.size() >= 32768) return "";
        buf.resize(buf.size() * 2);
    }
    const size_t slash = buf.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return "";
    return toUtf8(buf.substr(0, slash + 1));
}

std::vector<RunEntry> defaultEntries() {
    const std::string dir = exeDir();
    return {{kWinDaemonValue, dir + kWinDaemonExe, false}, {kWinUiValue, dir + kWinUiExe, true}};
}

}  // namespace

AutostartState queryRunEntries(const std::vector<RunEntry> &entries, std::string *detail) {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key);
    const bool noKey = rc == ERROR_FILE_NOT_FOUND;
    if (rc != ERROR_SUCCESS && !noKey) {
        if (detail) *detail = "Run key: " + winError(rc);
        return AutostartState::Error;
    }
    std::vector<Entry> es;
    for (const RunEntry &re : entries) {
        Entry e;
        e.name = re.valueName;
        e.optional = re.optional;
        std::string quoted;
        if (!fileExists(re.exePath)) {
            e.kind = Entry::Missing;
        } else if (!quoteRunValue(re.exePath, &quoted, &e.why)) {
            e.kind = Entry::Blocked;
        } else if (noKey) {
            e.kind = Entry::Absent;
        } else {
            const std::wstring name = toWide(re.valueName);
            DWORD type = 0, size = 0;
            rc = RegGetValueW(key, nullptr, name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                              &type, nullptr, &size);
            if (rc == ERROR_FILE_NOT_FOUND) {
                e.kind = Entry::Absent;
            } else if (rc == ERROR_UNSUPPORTED_TYPE) {
                e.kind = Entry::Different;
            } else if (rc != ERROR_SUCCESS) {
                e.kind = Entry::Failed;
                e.why = winError(rc);
            } else {
                std::wstring v(size / sizeof(wchar_t) + 1, L'\0');
                size = static_cast<DWORD>(v.size() * sizeof(wchar_t));
                rc = RegGetValueW(key, nullptr, name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                                  nullptr, &v[0], &size);
                if (rc != ERROR_SUCCESS) {
                    e.kind = Entry::Failed;
                    e.why = winError(rc);
                } else {
                    v.resize(wcslen(v.c_str()));
                    e.kind = toUtf8(v) == quoted ? Entry::Present : Entry::Different;
                }
            }
        }
        es.push_back(e);
    }
    if (key) RegCloseKey(key);
    return combineEntries(es, detail);
}

bool setRunEntries(const std::vector<RunEntry> &entries, bool enabled, std::string *err) {
    auto fail = [&](const std::string &m) { if (err) *err = m; return false; };
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return fail("Run key: " + winError(rc));
    bool ok = true;
    std::string msg;
    for (const RunEntry &re : entries) {
        const std::wstring name = toWide(re.valueName);
        if (!enabled) {
            rc = RegDeleteValueW(key, name.c_str());
            if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) { ok = false; msg = re.valueName + ": " + winError(rc); }
            continue;
        }
        if (!fileExists(re.exePath)) {
            if (re.optional) continue;
            ok = false;
            msg = re.valueName + ": program not found";
            continue;
        }
        std::string quoted, why;
        if (!quoteRunValue(re.exePath, &quoted, &why)) { ok = false; msg = re.valueName + ": " + why; continue; }
        const std::wstring v = toWide(quoted);
        if (v.empty()) { ok = false; msg = re.valueName + ": path is not valid UTF-8"; continue; }
        rc = RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(v.c_str()),
                            static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        if (rc != ERROR_SUCCESS) { ok = false; msg = re.valueName + ": " + winError(rc); }
    }
    RegCloseKey(key);
    return ok ? true : fail(msg);
}

AutostartState queryAutostart(std::string *detail) {
    return queryRunEntries(defaultEntries(), detail);
}

bool setAutostart(bool enabled, std::string *err) {
    return setRunEntries(defaultEntries(), enabled, err);
}

}  // namespace autostart
}  // namespace spn
