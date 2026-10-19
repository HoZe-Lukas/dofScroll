// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Command lines of the tray items "Restart daemon" and "Restart interface", per OS, as plain argv
// lists (no shell string anywhere). The Qt runner only executes them; everything that decides
// WHAT runs is here and unit-tested.
//
// Restart daemon (the keys are released first by the caller, see performTrayAction):
//   1. run `probe` (exit 0 = the daemon is managed by the service manager); if it succeeds, run
//      `managed` (restart through the service manager) and stop;
//   2. otherwise run `stop` (if any; waits), then start `spawn` detached.
// Restart interface: `managed` (Linux, running under dofscroll-ui.service: systemd restarts this
// process) or `relaunch`: start the same executable detached with `--restart`, then quit. The new
// instance waits for the single-instance lock of the old one (instance_paths.hpp).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/model/release_keys.hpp"

namespace spn::ui {

enum class Os { Linux, Windows, MacOS };

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
inline const char *macDaemonAgentLabel() { return "io.github.dofscroll.daemon"; }  // as in autostart_os.hpp

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
            const std::string target = "gui/" + std::to_string(uid) + "/" + macDaemonAgentLabel();
            p.probe = {"launchctl", "print", target};
            p.managed = {"launchctl", "kickstart", "-k", target};
            break;
        }
        case Os::Windows:
            // Same stop logic as the installer: quit event, then end the process, then release keys.
            p.stop = {"powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
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
