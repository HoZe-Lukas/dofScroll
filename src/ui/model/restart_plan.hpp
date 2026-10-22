// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Command lines of the two steps of the tray item "Restart" (daemon, then interface), per OS, as plain argv
// lists (no shell string anywhere). The Qt runner only executes them; everything that decides
// WHAT runs is here and unit-tested.
//
// Restart, daemon step (the keys are released first by the caller, see performTrayAction):
//   1. run `probe` (exit 0 = the daemon is managed by the service manager); if it succeeds, run
//      `managed` (restart through the service manager) and stop;
//   2. otherwise run `stop` (if any; waits), then start `spawn` detached.
// Restart, interface step: `managed` (Linux, running under dofscroll-ui.service: systemd restarts this
// process) or `relaunch`: start the same executable detached with `--restart`, then quit. The new
// instance waits for the single-instance lock of the old one (instance_paths.hpp).
#pragma once
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "platform/autostart_os.hpp"
#include "ui/model/release_keys.hpp"

namespace spn::ui {

enum class Os { Linux, Windows, MacOS };

// Windows PowerShell by its full path, so a "powershell.exe" next to the program (or in the current folder) is
// never started. SystemRoot is always set on Windows; elsewhere (the tests) the plain name stays.
inline std::string windowsPowerShell() {
    const char *root = std::getenv("SystemRoot");
    if (!root || !*root) return "powershell.exe";
    return std::string(root) + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

inline Os hostOs() {
#if defined(_WIN32)
    return Os::Windows;
#elif defined(__APPLE__)
    return Os::MacOS;
#else
    return Os::Linux;
#endif
}

using Argv = std::vector<std::string>;

inline const char *restartFlag() { return "--restart"; }

struct DaemonRestartPlan {
    Argv probe;    // empty: not probed, go on with `stop` / `spawn`
    Argv managed;  // run (and wait) when the probe succeeded
    Argv stop;     // run (and wait) before `spawn`; may be empty
    Argv spawn;    // started detached
};

inline std::string joinPath(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    return dir + (dir.back() == '/' || dir.back() == '\\' ? "" : "/") + name;
}

// exeDir: directory of the UI executable; uid: numeric user id (macOS launchd domain, else ignored).
inline DaemonRestartPlan daemonRestartPlan(Os os, const std::string &exeDir,
                                           const std::function<bool(const std::string &)> &isExecutable,
                                           unsigned uid) {
    DaemonRestartPlan p;
    p.spawn = {releaseKeysCommand(exeDir, isExecutable)[0]};
    switch (os) {
        case Os::Linux:
            p.probe = {"systemctl", "--user", "is-active", "--quiet", "dofscroll.service"};
            p.managed = {"systemctl", "--user", "restart", "dofscroll.service"};
            break;
        case Os::MacOS: {
            const std::string target = "gui/" + std::to_string(uid) + "/" + spn::autostart::kMacDaemonLabel;
            p.probe = {"launchctl", "print", target};
            p.managed = {"launchctl", "kickstart", "-k", target};
            break;
        }
        case Os::Windows:
            // Same stop logic as the installer: quit event, then end the process, then release keys.
            // The UI is a GUI process: no profile, no prompt, hidden window (the runner also sets
            // CREATE_NO_WINDOW, so no console flashes up).
            p.stop = {windowsPowerShell(), "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden",
                      "-ExecutionPolicy", "Bypass", "-File",
                      joinPath(joinPath(exeDir, "scripts"), "stop-daemon.ps1"), "-ExePath", p.spawn[0]};
            break;
    }
    return p;
}

struct UiRestartPlan {
    Argv managed;   // Linux under dofscroll-ui.service: run and wait; this process is replaced
    Argv relaunch;  // otherwise: start detached, then quit
};

// Arguments of this process without the program name; an earlier --restart is dropped.
inline Argv withoutRestartFlag(const Argv &args) {
    Argv out;
    for (const std::string &a : args)
        if (a != restartFlag()) out.push_back(a);
    return out;
}

// selfExe: path of this executable; args: its arguments (argv[1..]); underSystemd: running as
// dofscroll-ui.service (INVOCATION_ID set and the unit is active).
inline UiRestartPlan uiRestartPlan(Os os, const std::string &selfExe, const Argv &args, bool underSystemd) {
    UiRestartPlan p;
    if (os == Os::Linux && underSystemd) {
        p.managed = {"systemctl", "--user", "restart", "dofscroll-ui.service"};
        return p;
    }
    p.relaunch = {selfExe};
    for (const std::string &a : withoutRestartFlag(args)) p.relaunch.push_back(a);
    p.relaunch.push_back(restartFlag());
    return p;
}

// Probe for "running as dofscroll-ui.service" (run only when INVOCATION_ID is set).
inline Argv uiUnitProbe() { return {"systemctl", "--user", "is-active", "--quiet", "dofscroll-ui.service"}; }

// How long a start waits for the single-instance lock (ms): a restarted instance waits for the old
// one to exit, a normal start only lets the stale-lock detection run.
inline int lockWaitMs(bool restarted) { return restarted ? 8000 : 100; }

}  // namespace spn::ui
