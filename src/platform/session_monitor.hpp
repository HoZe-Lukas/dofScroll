// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Is the session this daemon runs in the one the user is looking at? The 3D mouse is one shared
// device: with several users logged in at once (fast user switching, remote desktop, multi-seat)
// every user's daemon receives its events, so the daemon of a background session must keep its
// output paused. This header holds the interface and the pure parsing/decision layer (unit-tested
// on every OS); the OS-specific probes live in src/platform/<os>/session_monitor_<os>.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "fs.hpp"

namespace spn {

// Poll-based and cheap: the daemon calls isActive() about once per second from its main loop. It
// must never block for long. Unknown (detection impossible, probe failed) means "active", so a
// missing or unreadable source never silences the daemon. `detail` (may be null) receives a short
// diagnostic text for the log.
class SessionMonitor {
public:
    virtual ~SessionMonitor() = default;
    virtual bool isActive(std::string *detail) = 0;
};

// One per OS (src/platform/<os>/session_monitor_<os>.cpp); never null.
std::unique_ptr<SessionMonitor> makeSessionMonitor();

enum class SessionState { Unknown, Active, Inactive };

// ---- Linux (logind) -----------------------------------------------------------------------------
// /run/systemd/sessions/<id> is a world-readable "KEY=VALUE" file. ACTIVE=1|0 is authoritative;
// without it STATE=active|online|closing decides ("active" = active, the others = not).
inline SessionState parseLogindSession(const std::string &text) {
    SessionState byActive = SessionState::Unknown, byState = SessionState::Unknown;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.compare(0, 7, "ACTIVE=") == 0) {
            const std::string v = line.substr(7);
            if (v == "1") byActive = SessionState::Active;
            else if (v == "0") byActive = SessionState::Inactive;
        } else if (line.compare(0, 6, "STATE=") == 0) {
            const std::string v = line.substr(6);
            if (v == "active") byState = SessionState::Active;
            else if (v == "online" || v == "closing") byState = SessionState::Inactive;
        }
    }
    return byActive != SessionState::Unknown ? byActive : byState;
}

// XDG_SESSION_ID is used as a file name: accept only a short run of [A-Za-z0-9_-] (no separators,
// no "..", so the variable can never point outside the sessions directory).
inline bool validLogindSessionId(const std::string &id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Reads <dir>/<id> (dir = /run/systemd/sessions in production). Links are refused (O_NOFOLLOW):
// the real session files are regular files in a root-owned directory, so refusing a link costs
// nothing and an unusual entry just counts as unknown (= active). Any failure = Unknown.
class LogindFileMonitor : public SessionMonitor {
public:
    LogindFileMonitor(std::string dir, std::string id) : dir_(std::move(dir)), id_(std::move(id)) {}
    bool isActive(std::string *detail) override {
        if (!validLogindSessionId(id_)) return note(detail, "no valid session id: assuming active", true);
        std::string text;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        const fs::ReadResult r = fs::readFileBounded(dir_ + "/" + id_, 4096, opt, &text);
        if (!r.ok()) return note(detail, "session file unreadable: assuming active", true);
        switch (parseLogindSession(text)) {
        case SessionState::Active: return note(detail, "logind session active", true);
        case SessionState::Inactive: return note(detail, "logind session not active", false);
        default: return note(detail, "logind state unknown: assuming active", true);
        }
    }
private:
    std::string dir_, id_;
    static bool note(std::string *detail, const char *text, bool active) {
        if (detail) *detail = text;
        return active;
    }
};

// ---- Windows --------------------------------------------------------------------------------------
// ownId = ProcessIdToSessionId (-1 on failure), consoleId = WTSGetActiveConsoleSessionId
// (0xFFFFFFFF = none), connectState = WTS_CONNECTSTATE_CLASS of the own session or -1 when the
// query failed.
constexpr int WTS_STATE_ACTIVE = 0;
inline SessionState decideWindowsSession(int64_t ownId, int64_t consoleId, int connectState) {
    if (ownId < 0) return SessionState::Unknown;
    if (ownId == consoleId) return SessionState::Active;
    if (connectState < 0) return SessionState::Unknown;
    return connectState == WTS_STATE_ACTIVE ? SessionState::Active : SessionState::Inactive;
}

// ---- macOS ----------------------------------------------------------------------------------------
// CGSessionCopyCurrentDictionary: kCGSessionOnConsoleKey is false while another user's session
// (fast user switching) owns the console. A missing dictionary or key is unknown.
inline SessionState decideMacSession(bool haveDictionary, bool haveKey, bool onConsole) {
    if (!haveDictionary || !haveKey) return SessionState::Unknown;
    return onConsole ? SessionState::Active : SessionState::Inactive;
}

}  // namespace spn
