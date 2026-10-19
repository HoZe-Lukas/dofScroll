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
// file without following links, requires the current user as owner, reads at most 64 bytes and
// accepts only a valid mode id (modes.hpp): no shell, no path, nothing else is ever interpreted.
// Leftovers of a daemon that was not running are removed at daemon start (clearModeRequest).
#pragma once
#include <string>

#include "modes.hpp"
#include "platform/fs.hpp"

namespace spn {

constexpr size_t kModeRequestMaxBytes = 64;

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
    return fs::writeFileReplace(modeRequestPath(runtimeDir), id + "\n", err);
}

inline void clearModeRequest(const std::string &runtimeDir) {
    const std::string p = modeRequestPath(runtimeDir);
    if (!p.empty()) fs::removeFile(p);
}

// Reads and removes the request. "" if there is none or it is not acceptable (the file is
// removed in every case where it existed).
inline std::string takeModeRequest(const std::string &runtimeDir) {
    const std::string p = modeRequestPath(runtimeDir);
    if (p.empty()) return "";
    std::string text;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    const fs::ReadResult r = fs::readFileBounded(p, kModeRequestMaxBytes, opt, &text);
    if (r.status == fs::ReadStatus::NotFound) return "";
    fs::removeFile(p);  // never follows a link at the end
    if (!r.ok()) return "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return validModeId(text) ? text : std::string();
}

}  // namespace spn
