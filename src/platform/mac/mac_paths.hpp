// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the macOS daemon (no OS calls, unit-tested on every platform in
// tests/test_mac_paths.cpp): directory layout, the reload socket path with the sun_path limit,
// descriptor directory search, the permissions status line and log trimming.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../path_rules.hpp"

namespace spn {

struct MacPaths {
    std::string configDir;   // ~/Library/Application Support/dofscroll
    std::string runtimeDir;  // ~/Library/Caches/dofscroll ("" when the home directory is unusable)
};

// `home` = $HOME (or the passwd entry). A missing or relative home gives a relative configDir (as
// on Linux) and an empty runtimeDir: without a safe per-user directory no status/log files.
inline MacPaths macPaths(const std::string &home) {
    MacPaths p;
    p.configDir = macConfigBase(home) + "/dofscroll";
    const std::string rt = macRuntimeBase(home);
    if (!rt.empty()) p.runtimeDir = rt + "/dofscroll";
    return p;
}

// sockaddr_un::sun_path is 104 bytes on macOS including the terminating NUL.
constexpr size_t kMacSunPathMax = 104;

struct ReloadSocket {
    std::string path;
    bool fallback = false;  // true: not inside the runtime dir (see below)
};

// Path of the reload socket: <runtimeDir>/reload.sock. If that does not fit into sun_path, use
// <tmpDir>/dofscroll-<uid>/reload.sock instead (tmpDir is a short directory such as "/tmp"); the
// caller must create that directory 0700 and verify owner and mode before binding or
// connecting. path is empty if nothing fits.
// socketPathFor does the same for any socket file name (the live feed uses "live.sock").
inline ReloadSocket socketPathFor(const char *file, const std::string &runtimeDir, unsigned long uid,
                                  const std::string &tmpDir) {
    ReloadSocket r;
    if (!runtimeDir.empty()) {
        const std::string p = runtimeDir + "/" + file;
        if (p.size() < kMacSunPathMax) {
            r.path = p;
            return r;
        }
    }
    const std::string f = stripTrailingSlashes(tmpDir) + "/dofscroll-" + std::to_string(uid) + "/" + file;
    r.fallback = true;
    if (!tmpDir.empty() && f.size() < kMacSunPathMax) r.path = f;
    return r;
}
inline ReloadSocket reloadSocketPath(const std::string &runtimeDir, unsigned long uid, const std::string &tmpDir) {
    return socketPathFor("reload.sock", runtimeDir, uid, tmpDir);
}

// Directory part of a socket path ("/a/b/reload.sock" -> "/a/b").
inline std::string socketDir(const std::string &socketPath) {
    const size_t i = socketPath.rfind('/');
    return i == std::string::npos ? std::string(".") : (i == 0 ? std::string("/") : socketPath.substr(0, i));
}

// Directories that may hold the devices/*.conf descriptions, in search order: $DOFSCROLL_DEVICES,
// <exe dir>/devices (build tree and app bundle layout), <exe dir>/../share/dofscroll/devices
// (installed under a prefix). Empty entries are skipped.
inline std::vector<std::string> descriptorDirCandidates(const std::string &envDir, const std::string &exeDir) {
    std::vector<std::string> c;
    if (!envDir.empty()) c.push_back(toSlashes(envDir));
    if (!exeDir.empty()) {
        c.push_back(exeDir + "/devices");
        c.push_back(exeDir + "/../share/dofscroll/devices");
    }
    return c;
}

// First candidate for which isDir() is true; the first candidate if none exists (so the log
// names a sensible directory), empty if there are no candidates.
inline std::string pickDescriptorDir(const std::vector<std::string> &candidates,
                                     const std::function<bool(const std::string &)> &isDir) {
    for (const std::string &d : candidates)
        if (isDir(d)) return d;
    return candidates.empty() ? std::string() : candidates.front();
}

// "permissions=ok" or "permissions=missing:accessibility,input_monitoring" (with newline); one
// line of the status file so the tray can show it instead of failing silently.
inline std::string permissionsStatusLine(bool accessibility, bool inputMonitoring) {
    if (accessibility && inputMonitoring) return "permissions=ok\n";
    std::string s = "permissions=missing:";
    if (!accessibility) s += "accessibility";
    if (!inputMonitoring) s += std::string(accessibility ? "" : ",") + "input_monitoring";
    return s + "\n";
}

// Human readable log hint for a missing permission (where to grant it).
inline std::string permissionHint(bool accessibility, bool inputMonitoring) {
    std::string s;
    if (!accessibility)
        s += "Accessibility permission missing: synthetic scroll/key events are blocked. Grant it in System "
             "Settings > Privacy & Security > Accessibility. ";
    if (!inputMonitoring)
        s += "Input Monitoring permission missing: the SpaceMouse cannot be read. Grant it in System "
             "Settings > Privacy & Security > Input Monitoring. ";
    if (!s.empty()) s.pop_back();
    return s;
}

// Number of '\n' in `text`.
inline size_t countLines(const std::string &text) {
    size_t n = 0;
    for (char c : text)
        if (c == '\n') ++n;
    return n;
}

// Rolling log: keeps the newest `keepLines` complete lines of `text` and at most `maxBytes` bytes
// (cut at a line start). A trailing partial line is dropped.
inline std::string trimLogText(const std::string &text, size_t keepLines, size_t maxBytes) {
    std::vector<size_t> starts;  // start offsets of complete lines
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) break;
        starts.push_back(pos);
        pos = nl + 1;
    }
    const size_t end = pos;  // end of the last complete line
    size_t first = starts.size() > keepLines ? starts.size() - keepLines : 0;
    while (first < starts.size() && end - starts[first] > maxBytes) ++first;
    if (first >= starts.size()) return std::string();
    return text.substr(starts[first], end - starts[first]);
}

// Reload socket trust decisions (pure, so they are unit-tested): the client only connects to a
// socket that belongs to it; the daemon only honours a peer with its own uid.
inline bool reloadSocketTrusted(bool isSocket, unsigned ownerUid, unsigned euid) { return isSocket && ownerUid == euid; }
inline bool reloadPeerAllowed(unsigned peerUid, unsigned euid) { return peerUid == euid; }

// Tracks the Accessibility / Input Monitoring state over time. update() says whether the state
// changed and whether a log line is due: immediately on a change, and a reminder every
// `repeatSec` while a permission is still missing (so the log is not flooded).
class PermissionTracker {
public:
    struct Result {
        bool changed = false;
        bool log = false;
    };
    explicit PermissionTracker(long long repeatSec = 600) : repeat_(repeatSec) {}
    Result update(bool accessibility, bool inputMonitoring, long long nowSec) {
        Result r;
        const bool first = !known_;
        r.changed = !first && (accessibility != ax_ || inputMonitoring != im_);
        const bool missing = !accessibility || !inputMonitoring;
        if (first) r.log = missing;
        else if (r.changed) r.log = true;
        else if (missing && nowSec - lastLog_ >= repeat_) r.log = true;
        if (r.log) lastLog_ = nowSec;
        known_ = true;
        ax_ = accessibility;
        im_ = inputMonitoring;
        return r;
    }

private:
    long long repeat_, lastLog_ = 0;
    bool known_ = false, ax_ = false, im_ = false;
};

// Exit codes: EX_CONFIG (78) = permanent error, retrying cannot help (launchd must not restart).
constexpr int kExitConfig = 78;
inline const char *launchdLabel() { return "io.github.dofscroll.daemon"; }
// launchd's KeepAlive/SuccessfulExit cannot distinguish exit codes: a failure exit would restart
// the daemon every ThrottleInterval forever. Under launchd (XPC_SERVICE_NAME is the job label) a
// permanent error therefore exits 0 ("do not restart"); in a terminal it is EX_CONFIG.
inline int permanentErrorExitCode(const char *xpcServiceName) {
    return xpcServiceName && std::string(xpcServiceName) == launchdLabel() ? 0 : kExitConfig;
}

// A second daemon finds the single-instance lock taken (platform/instance_lock.hpp): under launchd
// it exits 0 (a failure exit would restart it every ThrottleInterval), in a terminal EX_TEMPFAIL.
inline int alreadyRunningExitCode(const char *xpcServiceName) {
    return xpcServiceName && std::string(xpcServiceName) == launchdLabel() ? 0 : 75;
}

}  // namespace spn
