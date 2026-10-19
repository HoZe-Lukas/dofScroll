// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the Windows daemon (no OS calls, unit-tested on every platform by
// tests/test_win_paths.cpp): per-user directories from the environment, kernel object names,
// executable-name extraction for [app:*] profiles, descriptor search path, and the log/status
// line handling. The OS calls themselves are in win_platform.cpp and win_main.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "../path_rules.hpp"
#include "../../common.hpp"

namespace spn {

struct WinPaths {
    std::string configDir;   // %APPDATA%/dofscroll (roamed settings: dofscroll.conf, modes/)
    std::string runtimeDir;  // %LOCALAPPDATA%/dofscroll (status and log files, not roamed); may be empty
};

// `env` holds the variables APPDATA, LOCALAPPDATA and USERPROFILE (missing = not set). '/' separators.
// runtimeDir is empty when neither LOCALAPPDATA nor USERPROFILE gives an absolute path: no safe
// per-user directory, so the daemon then runs without status/log files.
inline WinPaths windowsPaths(const std::map<std::string, std::string> &env) {
    auto get = [&](const char *k) {
        auto it = env.find(k);
        return it == env.end() ? std::string() : it->second;
    };
    WinPaths p;
    p.configDir = windowsConfigBase(get("APPDATA"), get("USERPROFILE")) + "/dofscroll";
    const std::string rt = windowsRuntimeBase(get("LOCALAPPDATA"), get("USERPROFILE"));
    p.runtimeDir = rt.empty() ? std::string() : rt + "/dofscroll";
    return p;
}

// Profile key of [app:*] sections: lower-case file name of the executable, both separators accepted
// ("C:\Program Files\Mozilla Firefox\firefox.exe" -> "firefox.exe"). Only ASCII is lower-cased
// (non-ASCII bytes stay untouched).
inline std::string exeNameLower(const std::string &path) {
    const size_t cut = path.find_last_of("/\\");
    std::string n = cut == std::string::npos ? path : path.substr(cut + 1);
    for (char &c : n)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return n;
}

// Windows hosts packaged (UWP) apps in this process; its executable name says nothing about the
// real app, so the daemon reports "unknown" (empty) for it and the default profile applies.
inline bool isUwpHostExe(const std::string &exeLower) { return exeLower == "applicationframehost.exe"; }

// Kernel object names. "Local\" = the session's namespace: one daemon per logon session.
inline const char *reloadEventName() { return "Local\\dofscroll-reload"; }
// Set by stop-daemon.ps1 to ask the daemon to quit (it then releases its keys itself).
inline const char *quitEventName() { return "Local\\dofscroll-quit"; }
inline const char *singleInstanceMutexName() { return "Local\\dofscroll-single-instance"; }

// Directories searched (first existing wins) for the devices/*.conf descriptions: an explicit
// override (env DOFSCROLL_DEVICES) first, then next to the executable, then the install layout
// <prefix>/bin/dofscroll.exe -> <prefix>/share/dofscroll/devices.
inline std::vector<std::string> descriptorDirCandidates(const std::string &exeDir, const std::string &envOverride) {
    std::vector<std::string> c;
    if (!envOverride.empty()) c.push_back(toSlashes(envOverride));
    if (!exeDir.empty()) {
        c.push_back(exeDir + "/devices");
        c.push_back(exeDir + "/../share/dofscroll/devices");
    }
    return c;
}

// ---- log file ------------------------------------------------------------------------------------

// Prefixes every line of `chunk` that starts a line with `ts` (e.g. "2026-10-19 08:00:00 ") and
// appends the result to *out. *atLineStart carries the state across calls (a chunk may end in the
// middle of a line).
inline void stampLines(const std::string &chunk, bool *atLineStart, const std::string &ts, std::string *out) {
    for (char ch : chunk) {
        if (*atLineStart) {
            *out += ts;
            *atLineStart = false;
        }
        *out += ch;
        if (ch == '\n') *atLineStart = true;
    }
}

// Splits `data` into complete lines (without '\n'); an unterminated tail is dropped.
inline std::deque<std::string> splitLines(const std::string &data) {
    std::deque<std::string> lines;
    size_t pos = 0;
    while (pos < data.size()) {
        const size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) break;
        lines.push_back(data.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

// Rolling-log rule (same numbers as the Linux log, common.hpp): once more than maxLines lines or
// more than kLogMaxBytes bytes exist, only the newest keepLines remain. Returns true if the file
// has to be rewritten.
inline bool trimLogLines(std::deque<std::string> *lines, size_t maxLines, size_t keepLines, size_t fileBytes) {
    if (lines->size() <= maxLines && fileBytes <= kLogMaxBytes) return false;
    while (lines->size() > keepLines) lines->pop_front();
    // A few very long lines may still exceed the byte cap: drop more from the front.
    size_t bytes = 0;
    for (const std::string &l : *lines) bytes += l.size() + 1;
    while (bytes > kLogMaxBytes && !lines->empty()) {
        bytes -= lines->front().size() + 1;
        lines->pop_front();
    }
    return true;
}

inline std::string joinLines(const std::deque<std::string> &lines) {
    std::string s;
    for (const std::string &l : lines) {
        s += l;
        s += '\n';
    }
    return s;
}

// ---- status file ---------------------------------------------------------------------------------

// Throttle of the status file: written when the body changed or when `refreshNs` passed since the
// last write (the "updated" timestamp). Same rule as the Linux StatusWriter.
class StatusThrottle {
public:
    explicit StatusThrottle(int64_t refreshNs = 1'000'000'000) : refresh_(refreshNs) {}
    bool due(const std::string &body, int64_t nowNs) {
        if (body == last_ && nowNs < next_) return false;
        last_ = body;
        next_ = nowNs + refresh_;
        return true;
    }

private:
    int64_t refresh_;
    int64_t next_ = 0;
    std::string last_;
};

// Full file text: the core's "key=value" body plus the update time; "" if it would exceed the cap.
inline std::string statusFileText(const std::string &body, long long unixSeconds) {
    std::string text = body + "updated=" + std::to_string(unixSeconds) + "\n";
    return text.size() > kStatusMaxBytes ? std::string() : text;
}

}  // namespace spn
