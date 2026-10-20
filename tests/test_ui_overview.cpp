// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Overview model: one card per profile, the summary line, the status lines, the quick actions.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

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
    assert(profileSummary(p, id) == "Mode: timeline");
    p.overrides = {"SCROLL_V_SENS = 9", "BUTTON_1 = key:space"};
    assert(profileSummary(p, id) == "Mode: timeline, SCROLL_V_SENS = 9, BUTTON_1 = key:space");
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
    assert(d.profiles[1].summary == "Mode: timeline, BUTTON_1 = key:space");
    assert(d.profiles[2].name == "blender" && d.profiles[2].summary == "ENABLED = 0");
    assert(d.profiles[3].name == "gimp" && d.profiles[3].summary == "Uses the default settings");
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
    assert(d.daemonRunning && d.statusLine == "Profile: app:firefox  |  Mode: timeline");
    assert(d.infoLine == "Program: firefox  |  Device: SpaceMouse Pro" && d.warningLine.empty());

    const std::vector<OverviewAction> a = overviewActions();
    assert(a.size() == 3);
    assert(a[0].id == "release_keys" && a[0].action == TrayAction::ReleaseAllKeys);
    assert(a[1].id == "restart_daemon" && a[1].action == TrayAction::RestartDaemon);
    assert(a[2].id == "restart_interface" && a[2].action == TrayAction::RestartInterface);
    for (const OverviewAction &x : a) assert(!x.label.empty());
}

static void testGettingStarted() {
    IdentityStrings id;
    SettingsModel empty = loadModel("[global]\n");
    StatusModel st;
    OverviewData d = buildOverview(empty, st, id);
    std::vector<GettingStartedStep> steps = gettingStartedSteps(d, st, id);
    assert(steps.size() == 3 && steps[0].id == "device" && steps[1].id == "axes" && steps[2].id == "profile");
    assert(!steps[0].done && !steps[2].done && showGettingStarted(steps));
    assert(steps[1].page == "axes" && steps[2].page == "profiles" && steps[0].page.empty());
    for (const GettingStartedStep &x : steps) assert(!x.text.empty());
    // the daemon runs and sees a device
    st.update("profile=global\ndevice=Test Mouse\nmode=normal\nenabled=1\npaused=0\ndevices_seen=1\nupdated=100\n", true, 100);
    d = buildOverview(empty, st, id);
    steps = gettingStartedSteps(d, st, id);
    assert(steps[0].done && !steps[2].done && showGettingStarted(steps));
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
    testSummary();
    testProfilesAndCounts();
    testStatusAndActions();
    return 0;
}
