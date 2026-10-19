// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for restart_plan.hpp: the argv lists of "Restart daemon" / "Restart interface".
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/restart_plan.hpp"

using namespace spn::ui;

int main() {
    const auto none = [](const std::string &) { return false; };
    const auto all = [](const std::string &) { return true; };
    const std::string daemon = daemonExecutableName();

    // Linux: systemd first (probe + restart), the binary next to the UI as the fallback
    DaemonRestartPlan l = daemonRestartPlan(Os::Linux, "/usr/bin", all, 1000);
    assert((l.probe == Argv{"systemctl", "--user", "is-active", "--quiet", "dofscroll.service"}));
    assert((l.managed == Argv{"systemctl", "--user", "restart", "dofscroll.service"}));
    assert(l.stop.empty());
    assert((l.spawn == Argv{"/usr/bin/" + daemon}));
    assert((daemonRestartPlan(Os::Linux, "/opt/x", none, 1).spawn == Argv{daemon}));  // PATH lookup

    // macOS: launchd domain of the user, label of the LaunchAgent
    DaemonRestartPlan m = daemonRestartPlan(Os::MacOS, "/Applications/dofscroll.app/Contents/MacOS", none, 501);
    assert((m.probe == Argv{"launchctl", "print", "gui/501/io.github.dofscroll.daemon"}));
    assert((m.managed == Argv{"launchctl", "kickstart", "-k", "gui/501/io.github.dofscroll.daemon"}));
    assert(m.stop.empty() && m.spawn.size() == 1);

    // Windows: the installer's stop script, then start the daemon again; no probe, no service manager
    DaemonRestartPlan w = daemonRestartPlan(Os::Windows, "C:/Programs/dofscroll", all, 0);
    assert(w.probe.empty() && w.managed.empty());
    assert(w.stop.size() == 8 && w.stop[0] == "powershell.exe" && w.stop[5] == "C:/Programs/dofscroll/scripts/stop-daemon.ps1" &&
           w.stop[6] == "-ExePath" && w.stop[7] == w.spawn[0]);
    assert(w.spawn[0] == "C:/Programs/dofscroll/" + daemon);

    // never a shell
    for (const DaemonRestartPlan *p : {&l, &m, &w})
        for (const Argv *a : {&p->probe, &p->managed, &p->stop, &p->spawn})
            if (!a->empty()) assert((*a)[0] != "sh" && (*a)[0] != "/bin/sh" && (*a)[0] != "cmd" && (*a)[0] != "cmd.exe");

    // Interface: under the systemd unit, systemd restarts it
    UiRestartPlan u = uiRestartPlan(Os::Linux, "/usr/bin/dofscroll-ui", {}, true);
    assert((u.managed == Argv{"systemctl", "--user", "restart", "dofscroll-ui.service"}) && u.relaunch.empty());
    // otherwise relaunch with the same arguments plus --restart (an old --restart is not repeated)
    u = uiRestartPlan(Os::Linux, "/usr/bin/dofscroll-ui", {"-platform", "xcb", "--restart"}, false);
    assert(u.managed.empty());
    assert((u.relaunch == Argv{"/usr/bin/dofscroll-ui", "-platform", "xcb", "--restart"}));
    u = uiRestartPlan(Os::Windows, "C:/p/dofscroll-ui.exe", {}, true);  // systemd flag is ignored off Linux
    assert(u.managed.empty() && (u.relaunch == Argv{"C:/p/dofscroll-ui.exe", "--restart"}));
    u = uiRestartPlan(Os::MacOS, "/x/dofscroll-ui", {"a b"}, false);  // arguments stay single argv entries
    assert((u.relaunch == Argv{"/x/dofscroll-ui", "a b", "--restart"}));

    assert((uiUnitProbe() == Argv{"systemctl", "--user", "is-active", "--quiet", "dofscroll-ui.service"}));
    assert(lockWaitMs(true) > lockWaitMs(false) && lockWaitMs(false) == 100);

    std::puts("test_ui_restart_plan OK");
    return 0;
}
