// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Overview model: one card per profile, the summary line, the status lines, the quick actions.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

#include "mode_request.hpp"
#include "testutil.hpp"
#include "ui/model/overview_model.hpp"

using namespace spn;
using namespace spn::ui;

static SettingsModel loadModel(const std::string &text) {
    const std::string dir = tu::makeTempDir("dofscroll-overview");
    const std::string path = dir + "/dofscroll.conf";
    std::ofstream(path, std::ios::binary) << text;
    SettingsModel m;
    std::vector<std::string> warn;
    std::string err;
    assert(m.load(path, &warn, &err));
    return m;
}

static void testSummary() {
    IdentityStrings id;
    OverviewProfile p;
    assert(profileSummary(p, id) == "Uses the default settings");
    p.mode = "timeline";
    assert(profileSummary(p, id) == "Joystick mode: timeline");
    p.overrides = {"SCROLL_V_SENS = 9", "BUTTON_1 = key:space"};
    assert(profileSummary(p, id) == "Joystick mode: timeline, SCROLL_V_SENS = 9, BUTTON_1 = key:space");
    p.mode.clear();
    assert(profileSummary(p, id) == "SCROLL_V_SENS = 9, BUTTON_1 = key:space");
    // more than kMaxSummaryItems settings: the rest is counted
    p.overrides = {"A = 1", "B = 2", "C = 3", "D = 4", "E = 5"};
    assert(profileSummary(p, id) == "A = 1, B = 2, C = 3, +2 more");
    // a long value is clipped, never inside a UTF-8 sequence
    assert(clipValue("short") == "short");
    assert(clipValue(std::string(60, 'x')) == std::string(kMaxSummaryValueBytes, 'x') + "...");
    std::string two;
    for (int i = 0; i < 30; ++i) two += "\xC3\xA4";  // 60 bytes, 30 characters
    const std::string clipped = clipValue(two);
    assert(clipped.size() <= kMaxSummaryValueBytes + 3 && clipped.substr(clipped.size() - 3) == "...");
    assert(((clipped.size() - 3) % 2) == 0);  // whole characters only
}

static void testProfilesAndCounts() {
    IdentityStrings id;
    SettingsModel m = loadModel(
        "[global]\nMODE = normal\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n"
        "[app:firefox]\nMODE = timeline\nBUTTON_1 = key:space\n\n[app:blender]\nENABLED = 0\n\n[app:gimp]\n");
    StatusModel st;
    const OverviewData d = buildOverview(m, st, id);
    assert(d.profiles.size() == 4);
    assert(d.deviceProfiles == 1 && d.appProfiles == 3);
    assert(d.defaultMode == "normal");
    // file order, global is not a card
    assert(d.profiles[0].kind == Section::DEVICE && d.profiles[0].name == "SpaceMouse Pro");
    assert(d.profiles[0].mode.empty() && d.profiles[0].summary == "SCROLL_V_SENS = 9");
    assert(d.profiles[1].name == "firefox" && d.profiles[1].mode == "timeline");
    assert(d.profiles[1].summary == "Joystick mode: timeline, BUTTON_1 = key:space");
    assert(d.profiles[2].name == "blender" && d.profiles[2].summary == "ENABLED = 0");
    assert(d.profiles[3].name == "gimp" && d.profiles[3].summary == "Uses the default settings");
    // how much of a profile is its own, the rest is inherited
    assert(d.profiles[0].overrideCount == 1 && d.profiles[0].overrideText == "Overrides 1 setting");
    assert(d.profiles[1].overrideCount == 2 && d.profiles[1].overrideText == "Overrides 2 settings");
    assert(d.profiles[3].overrideCount == 0 && d.profiles[3].overrideText == "Overrides nothing: everything is inherited");
    // the section index is what the Profiles page selects
    for (const OverviewProfile &p : d.profiles) assert(m.sections()[p.section].pattern == p.name);
    // nothing configured: no cards
    SettingsModel empty = loadModel("[global]\n");
    assert(buildOverview(empty, st, id).profiles.empty());
}

static void testStatusAndActions() {
    IdentityStrings id;
    SettingsModel m = loadModel("[global]\nMODE = timeline\n");
    StatusModel st;
    OverviewData d = buildOverview(m, st, id);
    assert(!d.daemonRunning && d.statusLine == "Daemon not running / no status data" && d.infoLine.empty());
    assert(d.defaultMode == "timeline");
    st.update("profile=app:firefox\ndevice=SpaceMouse Pro\napp=firefox\nmode=timeline\nenabled=1\npaused=0\ndevices_seen=1\nupdated=1000\n", true, 1002);
    d = buildOverview(m, st, id);
    assert(d.daemonRunning && d.statusLine == "Profile: app:firefox  |  Joystick mode: timeline");
    assert(d.infoLine == "Program: firefox  |  Device: SpaceMouse Pro" && d.warningLine.empty());

    const std::vector<OverviewAction> a = overviewActions();
    assert(a.size() == 4);
    assert(a[0].id == "open_log" && a[0].action == TrayAction::OpenLog);
    assert(a[1].id == "release_keys" && a[1].action == TrayAction::ReleaseAllKeys);
    assert(a[2].id == "restart_daemon" && a[2].action == TrayAction::RestartDaemon);
    assert(a[3].id == "restart_interface" && a[3].action == TrayAction::RestartInterface);
    for (const OverviewAction &x : a) assert(!x.label.empty());
    // the open_log action runs the log hook of the view
    int shown = 0;
    TrayHooks hooks;
    hooks.showLog = [&shown] { ++shown; };
    RecordingActionRunner runner;
    TrayEntry log;
    log.action = a[0].action;
    std::string err;
    assert(performTrayAction(log, runner, hooks, &err) && shown == 1 && runner.calls.empty());
}

static void testTasks() {
    const std::vector<OverviewTask> t = overviewTasks();
    assert(t.size() == 3);
    assert(t[0].id == "tune" && t[0].page == "profile-channels");  // the Channels tab of the Global profile
    assert(t[1].id == "axes" && t[1].page == "axes");
    assert(t[2].id == "modes" && t[2].page == "modes");
    for (const OverviewTask &x : t) assert(!x.label.empty() && !x.tip.empty());
}

static void testButtonOverrides() {
    IdentityStrings id;
    // button overrides are settings like the others: named in the summary, all of them in the details
    SettingsModel m = loadModel("[global]\n\n[app:blender]\nMODE = timeline\nBUTTON_1 = key:space\nBUTTON_2 = key:enter\n"
                                "BUTTON_3 = nav\nBUTTON_4 = key:tab\n");
    StatusModel st;
    const OverviewData d = buildOverview(m, st, id);
    assert(d.profiles.size() == 1);
    const OverviewProfile &p = d.profiles[0];
    assert(p.overrides.size() == 4 && p.overrides[0] == "BUTTON_1 = key:space");
    assert(p.summary == "Joystick mode: timeline, BUTTON_1 = key:space, BUTTON_2 = key:enter, BUTTON_3 = nav, +1 more");
    assert(p.details == "Joystick mode: timeline\nBUTTON_1 = key:space\nBUTTON_2 = key:enter\nBUTTON_3 = nav\nBUTTON_4 = key:tab");
    OverviewProfile none;
    assert(profileDetails(none, id) == "Uses the default settings");
}

static void testFilterProfiles() {
    IdentityStrings id;
    SettingsModel m = loadModel(
        "[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\nBUTTON_1 = key:space\n\n"
        "[app:Blender]\nENABLED = 0\n");
    StatusModel st;
    const OverviewData d = buildOverview(m, st, id);
    assert(filterOverviewProfiles(d, "").size() == 3);
    assert(filterOverviewProfiles(d, "   ").size() == 3);
    assert((filterOverviewProfiles(d, "BLENDER") == std::vector<size_t>{2}));      // ASCII case folded
    assert((filterOverviewProfiles(d, "timeline") == std::vector<size_t>{1}));     // mode
    assert((filterOverviewProfiles(d, "button_1 space") == std::vector<size_t>{1}));  // two words, any field
    assert((filterOverviewProfiles(d, "sens") == std::vector<size_t>{0}));         // setting key
    assert((filterOverviewProfiles(d, "device") == std::vector<size_t>{0}));       // kind
    assert((filterOverviewProfiles(d, "program").size() == 2));
    assert(filterOverviewProfiles(d, "nothing like it").empty());
    assert(filterOverviewProfiles(d, "firefox blender").empty());  // every word has to match
}

static void testModeSwitch() {
    IdentityStrings id;
    SettingsModel m = loadModel("[global]\n");
    StatusModel st;
    const ModeSet ms = loadModes("", nullptr);  // built-in modes
    OverviewData d = buildOverview(m, st, id, &ms);
    assert(!d.modeChoices.empty() && d.activeMode == "normal");
    int active = 0;
    for (const auto &e : d.modeChoices) active += e.active;
    assert(active == 1);
    assert(buildOverview(m, st, id).modeChoices.empty());  // no modes given: nothing to switch to
    // the running daemon decides which mode is marked
    st.update("profile=global\ndevice=x\nmode=timeline\nenabled=1\npaused=0\ndevices_seen=1\nupdated=100\n", true, 100);
    d = buildOverview(m, st, id, &ms);
    assert(d.activeMode == "timeline");
    for (const auto &e : d.modeChoices) assert(e.active == (e.id == "timeline"));
    // a stale status is no running daemon: back to the default mode
    st.update("profile=global\nmode=timeline\nenabled=1\nupdated=100\n", true, 500);
    assert(buildOverview(m, st, id, &ms).activeMode == "normal");

    // picking a mode writes the request and asks for a reload, like the tray
    const std::string dir = tu::makeTempDir("dofscroll-overview-run");
    RecordingActionRunner runner;
    std::string err;
    assert(requestActiveMode(dir, runner, "timeline", &err));
    assert(runner.count(RecordingActionRunner::Kind::RequestReload) == 1);
    assert(spn::fs::pathExists(spn::modeRequestPath(dir)));
    // an invalid id or a missing runtime directory is refused before anything else happens
    RecordingActionRunner runner2;
    assert(!requestActiveMode(dir, runner2, "bad id!", &err) && !err.empty() && runner2.calls.empty());
    assert(!requestActiveMode("", runner2, "timeline", &err) && runner2.calls.empty());
    // a failing reload is reported
    RecordingActionRunner failing;
    failing.result = false;
    assert(!requestActiveMode(dir, failing, "timeline", &err) && err == "failed");
}

static void testGettingStarted() {
    IdentityStrings id;
    SettingsModel empty = loadModel("[global]\n");
    StatusModel st;
    OverviewData d = buildOverview(empty, st, id);
    std::vector<GettingStartedStep> steps = gettingStartedSteps(d, st, id);
    assert(steps.size() == 3 && steps[0].id == "device" && steps[1].id == "axes" && steps[2].id == "profile");
    assert(!steps[0].done && !steps[2].done && showGettingStarted(steps));
    // the device step links to the Axes page (it shows what the daemon sees) and says why it is open
    assert(steps[1].page == "axes" && steps[2].page == "profiles" && steps[0].page == "axes");
    assert(!steps[0].hint.empty() && steps[1].hint.empty() && steps[2].hint.empty());
    for (const GettingStartedStep &x : steps) assert(!x.text.empty());
    // the daemon runs and sees a device
    st.update("profile=global\ndevice=Test Mouse\nmode=normal\nenabled=1\npaused=0\ndevices_seen=1\nupdated=100\n", true, 100);
    d = buildOverview(empty, st, id);
    steps = gettingStartedSteps(d, st, id);
    assert(steps[0].done && !steps[2].done && showGettingStarted(steps));
    assert(steps[0].hint.empty());  // the daemon runs: nothing to hint
    // the axes step is done once the assistant was applied
    assert(!steps[1].done);
    steps = gettingStartedSteps(d, st, id, true);
    assert(steps[1].done && !steps[2].done && showGettingStarted(steps));
    // a profile exists: the guidance goes away (the axes step is advice, not a condition)
    SettingsModel one = loadModel("[global]\n\n[app:firefox]\n");
    d = buildOverview(one, st, id);
    steps = gettingStartedSteps(d, st, id);
    assert(steps[0].done && steps[2].done && !steps[1].done && !showGettingStarted(steps));
    // a profile without a seen device still shows it
    StatusModel none;
    d = buildOverview(one, none, id);
    steps = gettingStartedSteps(d, none, id);
    assert(!steps[0].done && steps[2].done && showGettingStarted(steps));
}

int main() {
    testGettingStarted();
    testTasks();
    testButtonOverrides();
    testFilterProfiles();
    testModeSwitch();
    testSummary();
    testProfilesAndCounts();
    testStatusAndActions();
    return 0;
}
