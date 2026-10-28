// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests of the first start: the daemon start plan and the grace period before the interface starts
// the daemon (restart_plan.hpp, daemon_autostart.hpp), the macOS bundle path of the daemon, the live
// checks and the skip / finish flow of the setup guide (setup_guide_model.hpp).
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/fs.hpp"
#include "testutil.hpp"
#include "ui/model/daemon_autostart.hpp"
#include "ui/model/release_keys.hpp"
#include "ui/model/restart_plan.hpp"
#include "ui/model/setup_guide_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/ui_progress.hpp"

using namespace spn::ui;

static const SetupStep *find(const std::vector<SetupStep> &v, const std::string &id) {
    for (const SetupStep &s : v)
        if (s.id == id) return &s;
    return nullptr;
}

static void testStartPlan() {
    const auto none = [](const std::string &) { return false; };
    const auto all = [](const std::string &) { return true; };
    const std::string daemon = daemonExecutableName();

    // Linux: systemd starts the unit (no probe needed), the binary next to the interface is the fallback
    DaemonStartPlan l = daemonStartPlan(Os::Linux, "/usr/bin", all, 1000);
    assert(l.probe.empty());
    assert((l.managed == Argv{"systemctl", "--user", "start", "dofscroll.service"}));
    assert((l.spawn == Argv{"/usr/bin/" + daemon}));
    assert((daemonStartPlan(Os::Linux, "/opt/x", none, 1).spawn == Argv{daemon}));

    // macOS: kickstart (without -k: a running job is left alone) only when the agent is loaded; else the bundle's daemon
    const std::string macui = "/Applications/dofscroll.app/Contents/MacOS";
    const std::string macdaemon = "/Applications/dofscroll.app/Contents/Resources/daemon/dofscroll";
    DaemonStartPlan m = daemonStartPlan(Os::MacOS, macui, [&](const std::string &p) { return p == macdaemon; }, 501);
    assert((m.probe == Argv{"launchctl", "print", "gui/501/io.github.dofscroll.daemon"}));
    assert((m.managed == Argv{"launchctl", "kickstart", "gui/501/io.github.dofscroll.daemon"}));
    assert((m.spawn == Argv{macdaemon}));

    // Windows: the daemon next to the interface, no service manager
    DaemonStartPlan w = daemonStartPlan(Os::Windows, "C:/Programs/dofscroll", all, 0);
    assert(w.probe.empty() && w.managed.empty() && w.spawn[0] == "C:/Programs/dofscroll/" + daemon);

    // never a shell; helper programs are bare names the fixed-directory lookup resolves
    for (const DaemonStartPlan *p : {&l, &m, &w})
        for (const Argv *a : {&p->probe, &p->managed, &p->spawn})
            if (!a->empty()) assert((*a)[0] != "sh" && (*a)[0] != "/bin/sh" && (*a)[0] != "cmd" && (*a)[0] != "cmd.exe");
    Argv sc = l.managed;
    assert(resolveHelperArgv(&sc, [](const std::string &n) { return "/usr/bin/" + n; }) && sc[0] == "/usr/bin/systemctl");
    assert((spacenavdProbe() == Argv{"systemctl", "is-active", "--quiet", "spacenavd.service"}));

    // the macOS bundle path
    assert(spn::autostart::macBundleDaemonPath(macui) == macdaemon);
    assert(spn::autostart::macBundleDaemonPath(macui + "/") == macdaemon);
    assert(spn::autostart::macBundleDaemonPath("/usr/bin").empty());
    assert(spn::autostart::macBundleDaemonPath("/opt/Contents/MacOSX").empty());
    // the release-keys command finds the bundled daemon too, and prefers a daemon next to the interface
    assert(releaseKeysCommand(macui, [&](const std::string &p) { return p == macdaemon; })[0] == macdaemon);
    assert(releaseKeysCommand(macui, all)[0] == macui + "/" + daemon);
    assert(releaseKeysCommand(macui, none)[0] == daemon);
    std::puts("ok start plan");
}

static void testAutoStart() {
    // the daemon is running at once (login autostart started both): never started
    {
        DaemonAutoStart a;
        assert(a.tick(0, true) == DaemonAutoStart::Step::Done && a.done() && !a.attempted());
        assert(a.tick(60000, false) == DaemonAutoStart::Step::Done);  // stopped later on purpose: stays stopped
    }
    // the heartbeat appears inside the grace period: never started
    {
        DaemonAutoStart a;
        assert(a.tick(1000, false) == DaemonAutoStart::Step::Wait && a.waiting(1000));
        assert(a.tick(3000, false) == DaemonAutoStart::Step::Wait);
        assert(a.tick(5000, true) == DaemonAutoStart::Step::Done && !a.attempted() && !a.waiting(5500));
    }
    // no heartbeat after the grace period: started once, not again
    {
        DaemonAutoStart a;
        assert(a.tick(5999, false) == DaemonAutoStart::Step::Wait);
        assert(a.tick(DaemonAutoStart::kGraceMs, false) == DaemonAutoStart::Step::Start && a.attempted());
        assert(a.tick(8000, false) == DaemonAutoStart::Step::Done && a.tick(9000, false) == DaemonAutoStart::Step::Done);
        assert(!a.waiting(9000));
    }
    assert(DaemonAutoStart::kGraceMs >= 5000 && DaemonAutoStart::kGraceMs <= 10000);
    std::puts("ok auto start");
}

static std::string statusText(long now, const std::string &extra) {
    return "profile=global\nenabled=1\nupdated=" + std::to_string(now) + "\n" + extra;
}

static void testStatusPermissions() {
    DaemonStatus d = parseStatus("permissions=ok\n");
    assert(d.permissions_known && !d.missing_accessibility && !d.missing_input_monitoring);
    d = parseStatus("permissions=missing:accessibility,input_monitoring\n");
    assert(d.permissions_known && d.missing_accessibility && d.missing_input_monitoring);
    d = parseStatus("permissions=missing:input_monitoring\n");
    assert(d.permissions_known && !d.missing_accessibility && d.missing_input_monitoring);
    d = parseStatus("profile=global\n");
    assert(!d.permissions_known);
    std::puts("ok status permissions");
}

static void testGuideRows() {
    IdentityStrings s;
    // nothing yet, inside the grace period (Linux)
    SetupInputs in;
    in.os = Os::Linux;
    in.waitingForDaemon = true;
    std::vector<SetupStep> g = buildSetupGuide(in, s);
    assert(find(g, "daemon")->state == StepState::Todo && find(g, "daemon")->action == StepAction::None);
    assert(find(g, "device")->state == StepState::Todo);
    assert(!find(g, "accessibility") && !find(g, "input-monitoring"));  // macOS only
    assert(find(g, "tryit") && find(g, "tryit")->state == StepState::Info && find(g, "tryit")->status.empty());
    for (const SetupStep &st : g) assert(st.state == StepState::Info || !st.status.empty());  // words, not only colour

    // grace period over, no daemon: needs attention, "Start now"
    in.waitingForDaemon = false;
    in.daemonStartFailed = true;
    g = buildSetupGuide(in, s);
    assert(find(g, "daemon")->state == StepState::Problem && find(g, "daemon")->action == StepAction::StartDaemon &&
           !find(g, "daemon")->actionLabel.empty());

    // running, device not found, spacenavd off (Linux): plain words plus the command
    in.daemonRunning = true;
    in.spacenavd = ServiceState::Inactive;
    g = buildSetupGuide(in, s);
    assert(find(g, "daemon")->state == StepState::Done && find(g, "daemon")->status == "Done");
    const SetupStep *dev = find(g, "device");
    assert(dev->state == StepState::Problem && dev->command == "sudo systemctl enable --now spacenavd.service");
    assert(dev->text.find("spacenavd") != std::string::npos);
    // spacenavd unknown or active: no command, just the plain hint
    in.spacenavd = ServiceState::Active;
    assert(find(buildSetupGuide(in, s), "device")->command.empty() && find(buildSetupGuide(in, s), "device")->state == StepState::Todo);
    in.spacenavd = ServiceState::Unknown;
    assert(find(buildSetupGuide(in, s), "device")->command.empty());
    // the command is a Linux thing
    in.os = Os::Windows;
    in.spacenavd = ServiceState::Inactive;
    assert(find(buildSetupGuide(in, s), "device")->command.empty());

    // found
    in.os = Os::Linux;
    in.deviceSeen = true;
    in.deviceName = "SpaceMouse Pro";
    g = buildSetupGuide(in, s);
    assert(find(g, "device")->state == StepState::Done && find(g, "device")->text.find("SpaceMouse Pro") != std::string::npos);
    in.inputIssue = spn::InputIssue::Blocked;
    assert(find(buildSetupGuide(in, s), "device")->state == StepState::Problem);
    in.inputIssue = spn::InputIssue::Clear;

    // start at login
    assert(find(g, "autostart")->state == StepState::Todo && find(g, "autostart")->action == StepAction::Autostart);
    in.autostartKnown = true;
    in.autostartOn = true;
    assert(find(buildSetupGuide(in, s), "autostart")->state == StepState::Done);
    in.autostartOn = false;
    assert(find(buildSetupGuide(in, s), "autostart")->state == StepState::Todo);
    in.autostartDetail = "unit not found";
    assert(find(buildSetupGuide(in, s), "autostart")->state == StepState::Problem);

    // macOS: both permissions, with a button each
    SetupInputs mac;
    mac.os = Os::MacOS;
    mac.daemonRunning = true;
    mac.daemonPath = "/Applications/dofscroll.app/Contents/Resources/daemon/dofscroll";
    g = buildSetupGuide(mac, s);
    assert(find(g, "accessibility")->state == StepState::Todo && find(g, "input-monitoring")->state == StepState::Todo);  // not reported yet
    mac.permissionsKnown = true;
    mac.missingAccessibility = true;
    g = buildSetupGuide(mac, s);
    const SetupStep *ax = find(g, "accessibility");
    assert(ax->state == StepState::Problem && ax->action == StepAction::OpenAccessibility && !ax->actionLabel.empty());
    assert(ax->text.find(mac.daemonPath) != std::string::npos);
    assert(find(g, "input-monitoring")->state == StepState::Done && find(g, "input-monitoring")->action == StepAction::OpenInputMonitoring);
    mac.missingAccessibility = false;
    g = buildSetupGuide(mac, s);
    assert(find(g, "accessibility")->state == StepState::Done && find(g, "input-monitoring")->state == StepState::Done);
    size_t total = 0;
    assert(setupStepsDone(g, &total) == 3 && total == 5);  // daemon and both permissions; device and autostart are open
    std::puts("ok guide rows");
}

// Consent for "Start at login" (issue #276): the guide never changes the system by itself. It shows the box (on by default at
// the first run), and the system is changed only when the user confirms the guide and the box is ticked or unticked on purpose.
static void testAutostartConsent() {
    // no system change at construction: building the model and reading the state asks for nothing
    AutostartConsent first(true);
    assert(!first.cancel().needed);                              // nothing is done unless the user confirms
    assert(first.wish());                                        // the visible default of the first run is "on"
    first.systemState(true, false, false, true);                 // read: off
    assert(first.wish() && first.pending() == AutostartConsent::Pending::Enable);  // announced, not done
    // cancel (Skip, Escape, the close button): nothing is changed
    assert(!first.cancel().needed);
    // confirm with the box ticked (the default): switched on, once
    AutostartConsent::Change c = first.confirm();
    assert(c.needed && c.enable);

    // first run, already on in the system: confirming changes nothing
    AutostartConsent already(true);
    already.systemState(true, true, false, true);
    assert(already.pending() == AutostartConsent::Pending::None && !already.confirm().needed);

    // first run, the user unticks: confirm leaves it off (nothing to change if it is off), cancel changes nothing
    AutostartConsent off(true);
    off.systemState(true, false, false, true);
    off.setWish(false);
    assert(!off.wish() && !off.confirm().needed && !off.cancel().needed);
    // first run, on in the system, the user unticks: confirm switches it off
    AutostartConsent unt(true);
    unt.systemState(true, true, false, true);
    unt.setWish(false);
    assert(unt.pending() == AutostartConsent::Pending::Disable);
    c = unt.confirm();
    assert(c.needed && !c.enable);
    assert(!unt.cancel().needed);

    // opened again later (not the first run): the box shows the system state; confirming without a click changes nothing
    AutostartConsent again(false);
    again.systemState(true, true, false, true);
    assert(again.wish() && again.pending() == AutostartConsent::Pending::None && !again.confirm().needed);
    AutostartConsent again2(false);
    again2.systemState(true, false, false, true);
    assert(!again2.wish() && !again2.confirm().needed);
    // ... a click is a decision that is applied on confirm only
    again2.setWish(true);
    assert(again2.pending() == AutostartConsent::Pending::Enable && !again2.cancel().needed);
    c = again2.confirm();
    assert(c.needed && c.enable);

    // a partly active setup is repaired on confirm when the wish is "on", and removed when the wish is "off"
    AutostartConsent part(false);
    part.systemState(true, false, true, true);
    part.setWish(true);
    assert(part.confirm().needed && part.confirm().enable);
    part.setWish(false);
    assert(part.confirm().needed && !part.confirm().enable);

    // not changeable here (no systemd, entry cannot be written): never a change, whatever the wish
    AutostartConsent nope(true);
    nope.systemState(true, false, false, false);
    assert(!nope.confirm().needed && nope.pending() == AutostartConsent::Pending::None);

    // the state is not read yet: a first run still has its wish; the change is applied on confirm (switching on twice is harmless)
    AutostartConsent unread(true);
    c = unread.confirm();
    assert(c.needed && c.enable);
    AutostartConsent unread2(false);
    assert(!unread2.confirm().needed);

    // the guide rows tell what will happen, in words, and are not "Done" before it happened
    IdentityStrings s;
    SetupInputs in;
    in.autostartKnown = true;
    in.autostartOn = false;
    in.autostartPending = AutostartConsent::Pending::Enable;
    const std::vector<SetupStep> g = buildSetupGuide(in, s);
    assert(find(g, "autostart")->state == StepState::Todo && find(g, "autostart")->text.find("Finish") != std::string::npos);
    in.autostartOn = true;
    in.autostartPending = AutostartConsent::Pending::Disable;
    assert(find(buildSetupGuide(in, s), "autostart")->state == StepState::Todo);
    std::puts("ok autostart consent");
}

static void testSettingsUrls() {
    assert(isSystemSettingsUrl(permissionPaneUrl(PermissionPane::Accessibility)));
    assert(isSystemSettingsUrl(permissionPaneUrl(PermissionPane::InputMonitoring)));
    assert(!isSystemSettingsUrl("https://example.org/") && !isSystemSettingsUrl("file:///etc/passwd") && !isSystemSettingsUrl(""));
    assert(!isSystemSettingsUrl(std::string(permissionPaneUrl(PermissionPane::Accessibility)) + "&x=1"));
    // a status derived from the daemon file
    StatusModel st;
    st.update(statusText(1000, "devices_seen=1\ndevice=Pro\npermissions=missing:accessibility\n"), true, 1000);
    SetupInputs in = setupInputsFrom(st, Os::MacOS);
    assert(in.daemonRunning && in.deviceSeen && in.deviceName == "Pro" && in.permissionsKnown && in.missingAccessibility &&
           !in.missingInputMonitoring);
    st.update("", false, 1000);
    in = setupInputsFrom(st, Os::Linux);
    assert(!in.daemonRunning && !in.deviceSeen && !in.permissionsKnown);
    std::puts("ok settings urls");
}

static void testFlow() {
    const std::string dir = tu::makeTempDir("dofscroll-guide");
    const std::string path = dir + "/ui-progress.conf";
    std::string err;

    // a new installation: shown at the first start, skipped once, not shown again
    {
        SetupGuideFlow f(path);
        assert(f.start(true) && f.visible());
        assert(f.skip(&err) && !f.visible());
    }
    assert(loadUiProgress(path).setupGuideSeen);
    {
        SetupGuideFlow f(path);
        assert(!f.start(true) && !f.visible());  // seen: not by itself
        f.show();                                // "Show setup guide" on the Overview
        assert(f.visible());
        assert(f.finish(&err) && !f.visible());
    }
    // other progress is kept
    UiProgress p = loadUiProgress(path);
    p.axisAssistantApplied = true;
    assert(saveUiProgress(path, p, &err));
    assert(markSetupGuideSeen(path, &err));
    assert(loadUiProgress(path).axisAssistantApplied && loadUiProgress(path).setupGuideSeen);

    // an existing installation never gets the guide by itself, and is marked as seen
    tu::stdfs::create_directories(dir + "/second");
    const std::string path2 = dir + "/second/ui-progress.conf";
    {
        SetupGuideFlow f(path2);
        assert(!f.start(false) && !f.visible());
    }
    {
        SetupGuideFlow f(path2);
        f.show();
        assert(f.visible() && f.closed(&err) && !f.visible());
    }
    assert(setupGuideDue(UiProgress(), true) && !setupGuideDue(UiProgress(), false));
    UiProgress seen;
    seen.setupGuideSeen = true;
    assert(!setupGuideDue(seen, true));
    // the file format: one more line, read back
    assert(parseUiProgress(serializeUiProgress(seen)).setupGuideSeen && !parseUiProgress("axis_assistant_applied = 1\n").setupGuideSeen);
    tu::removeAll(dir);
    std::puts("ok flow");
}

int main() {
    testStartPlan();
    testAutoStart();
    testStatusPermissions();
    testGuideRows();
    testAutostartConsent();
    testSettingsUrls();
    testFlow();
    std::puts("test_ui_setup_guide: ok");
    return 0;
}
