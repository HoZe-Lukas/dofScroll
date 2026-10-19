// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Where the Qt UI keeps its single-instance lock and the "raise your window" marker.
// Both live in the per-user runtime directory (PlatformPaths::runtimeDir: Linux $XDG_RUNTIME_DIR,
// Windows %LOCALAPPDATA%/dofscroll, macOS per-user cache/runtime folder). On Linux that directory
// is writable inside the systemd user unit (ReadWritePaths=%t) and is cleaned at logout, so a
// lock never survives a reboot. The ~/.local/share directory must NOT be used: the unit mounts
// the home directory read-only. If the OS offers no runtime directory (empty), the config
// directory (always writable for the UI) is the fallback.
#pragma once
#include <string>

namespace spn {
namespace ui {

inline std::string instanceDir(const std::string &runtimeDir, const std::string &configDir) {
    return runtimeDir.empty() ? configDir : runtimeDir;
}
inline std::string instanceLockPath(const std::string &dir) { return dir + "/dofscroll-ui.lock"; }
inline std::string instanceRaisePath(const std::string &dir) { return dir + "/dofscroll-ui.raise"; }

// What a failed lock attempt means. Mirrors QLockFile::LockError so the logic is testable without
// Qt: only "locked by another live process" is the normal second-start case; everything else
// (no permission, unknown error) is a real failure that must not be reported as success.
enum class LockOutcome { AlreadyRunning, Failure };
inline LockOutcome classifyLockFailure(bool lockFailedError) {
    return lockFailedError ? LockOutcome::AlreadyRunning : LockOutcome::Failure;
}

// Exit codes of dofscroll-ui for the start-up cases.
constexpr int kExitAlreadyRunning = 0;  // the running instance was asked to show its window
constexpr int kExitLockFailure = 1;

}  // namespace ui
}  // namespace spn
