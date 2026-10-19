// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Runtime mode request: how the tray (or any UI) asks the running daemon to switch the active
// mode without touching the configuration (the default mode in [global] stays as it is).
//
//   UI:     writeModeRequest(runtimeDir, "timeline", &err); requestReload(&err);
//   Daemon: on a reload request, after the config and modes were re-read: takeModeRequest(dir)
//           returns the id (and removes the file); if the mode is loaded it becomes the active
//           mode like a button switch (mode:<id>), until the next reload, restart or switch.
//
// The request is the file "<runtimeDir>/dofscroll-mode-request" holding one line, the mode id.
// The runtime directory is the per-user one (PlatformPaths::runtimeDir). The reader opens the
// file without following links, requires the current user as owner and user-only permissions
// (POSIX 0600), reads at most 64 bytes and accepts only a valid mode id (modes.hpp): no shell, no
// path, nothing else is ever interpreted. The writer never follows a link either (it removes
// whatever is at the name and creates the file exclusively, user-only, complete or not at all).
// A request older than kModeRequestMaxAgeSeconds is stale (the reload never reached the daemon)
// and ignored; leftovers of a daemon that was not running are removed at daemon start
// (clearModeRequest). In every case where a file existed, the reader removes it.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>

#include "modes.hpp"
#include "platform/fs.hpp"

namespace spn {

constexpr size_t kModeRequestMaxBytes = 64;
constexpr int64_t kModeRequestMaxAgeSeconds = 30;

inline std::string modeRequestPath(const std::string &runtimeDir) {
    return runtimeDir.empty() ? std::string() : runtimeDir + "/dofscroll-mode-request";
}

// false + *err: empty runtime directory, invalid id or write failure.
inline bool writeModeRequest(const std::string &runtimeDir, const std::string &id, std::string *err) {
    if (runtimeDir.empty()) {
        if (err) *err = "no runtime directory: the daemon cannot be asked to switch the mode";
        return false;
    }
    if (!validModeId(id)) {
        if (err) *err = "invalid mode id";
        return false;
    }
    const std::string path = modeRequestPath(runtimeDir);
    if (!fs::mkdirsPrivate(runtimeDir, err)) return false;
    // unlink never follows a link (it removes the link itself); createFileExclusive refuses to
    // overwrite anything, uses 0600 / a user-only ACL and publishes the complete content at once.
    fs::removeFile(path);
    if (!fs::createFileExclusive(path, id + "\n")) {
        if (err) *err = "cannot write the mode request " + path;
        return false;
    }
    return true;
}

inline void clearModeRequest(const std::string &runtimeDir) {
    const std::string p = modeRequestPath(runtimeDir);
    if (!p.empty()) fs::removeFile(p);
}

inline int64_t systemNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Reads and removes the request. "" if there is none or it is not acceptable (the file is
// removed in every case where it existed). nowNs is the clock for the staleness check (tests).
inline std::string takeModeRequest(const std::string &runtimeDir, int64_t nowNs = systemNowNs()) {
    const std::string p = modeRequestPath(runtimeDir);
    if (p.empty()) return "";
    std::string text;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    opt.requirePrivate = true;
    const fs::ReadResult r = fs::readFileBounded(p, kModeRequestMaxBytes, opt, &text);
    if (r.status == fs::ReadStatus::NotFound) return "";
    fs::FileInfo fi;
    const bool haveInfo = fs::lstatPath(p, &fi);
    fs::removeFile(p);  // never follows a link at the end
    if (!r.ok() || !haveInfo || fi.isLink) return "";
    // stale: written long ago (the reload never arrived); a time stamp in the future is as suspect
    const int64_t age = nowNs - fi.mtimeNs, max = kModeRequestMaxAgeSeconds * 1000000000LL;
    if (age > max || age < -max) return "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return validModeId(text) ? text : std::string();
}

}  // namespace spn
