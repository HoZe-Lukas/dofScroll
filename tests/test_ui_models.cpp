// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/*.hpp (status, tray menu, curve preview, action runner).
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/action_runner.hpp"
#include "ui/model/curve_preview.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"
#include "ui/model/tray_menu_model.hpp"

using namespace spn;
using namespace spn::ui;

struct Upper : StringProvider {
    std::string tr(const char *s) const override { return std::string("T:") + s; }
};

static const char *kFile =
    "profile=app:firefox\ndevice=SpaceMouse Pro\napp=firefox\nmode=timeline\nenabled=1\npaused=0\n"
    "devices_seen=1\nupdated=1000\n";

static void testStatus() {
    IdentityStrings id;
    assert(format("a %1 b %2 %3", {"x", "y"}) == "a x b y %3");
    DaemonStatus d = parseStatus(kFile);
    assert(d.profile == "app:firefox" && d.mode == "timeline" && d.enabled && !d.paused);
    assert(d.has_updated && d.updated == 1000 && d.devices_seen == 1);

    StatusModel m;
    assert(m.state() == StatusModel::State::NoStatus);
    m.update("", false, 1000);
    assert(m.state() == StatusModel::State::NoStatus && !m.daemonRunning());
    assert(m.mainLine(id) == "Daemon not running / no status data" && m.infoLine(id).empty());
    m.update(kFile, true, 1003);
    assert(m.state() == StatusModel::State::Running && m.daemonRunning());
    assert(m.mainLine(id) == "Profile: app:firefox  |  Joystick mode: timeline");
    assert(m.mainLine(id, "Timeline") == "Profile: app:firefox  |  Joystick mode: Timeline");
    assert(m.infoLine(id) == "Program: firefox  |  Device: SpaceMouse Pro");
    assert(m.warningLine(id).empty());
    {  // toggle= : the navigation latch is shown, a toggled mode id containing "nav" is not mistaken for it
        StatusModel t;
        t.update(std::string(kFile) + "toggle=nav\n", true, 1003);
        assert(t.mainLine(id) == "Profile: app:firefox  |  Joystick mode: timeline  |  Navigation on");
        t.update(std::string(kFile) + "toggle=mode:navigator\n", true, 1003);
        assert(t.mainLine(id) == "Profile: app:firefox  |  Joystick mode: timeline");
        t.update(std::string(kFile) + "toggle=nav mode:x\n", true, 1003);
        assert(t.mainLine(id).find("Navigation on") != std::string::npos);
    }
    assert(m.title(id) == "dofscroll - Profile: app:firefox  |  Joystick mode: timeline");
    // Staleness in both directions, and without "updated".
    m.update(kFile, true, 1006);
    assert(m.state() == StatusModel::State::Stale && !m.daemonRunning());
    m.update(kFile, true, 994);
    assert(m.state() == StatusModel::State::Stale);
    m.update("profile=x\n", true, 1000);
    assert(m.state() == StatusModel::State::Stale);
    // Paused / disabled / several devices / empty profile / hostile text.
    m.update("paused=1\nenabled=0\ndevices_seen=2\ndevice=A\nupdated=5\n", true, 5);
    assert(m.state() == StatusModel::State::Paused);
    assert(m.mainLine(id) == "Profile: global (paused) (disabled)");
    assert(m.warningLine(id) == "2 SpaceMice connected, only 'A' is reported");
    // One lane per device: no "only A is reported" warning, a device list instead.
    m.update("profile=global\ndevice=A\nmode=normal\ndevices_seen=2\nlanes=2\ndevice2=B\nmode2=timeline\nupdated=5\n", true, 5);
    assert(m.warningLine(id).empty());
    assert(m.status().more.size() == 1 && m.status().more[0].device == "B" && m.status().more[0].mode == "timeline");
    assert(m.devicesLine(id) == "Devices: A (Joystick mode: normal), B (Joystick mode: timeline)");
    assert(m.infoLine(id).find("Devices: A") != std::string::npos);
    m.update("device2=B\nlanes=1\nupdated=5\n", true, 5);
    assert(m.devicesLine(id).empty());
    m.update("enabled=0\nupdated=5\r\n", true, 5);
    assert(m.state() == StatusModel::State::Disabled);
    m.update("profile=a\x1b[31mb\nupdated=5\n", true, 5);
    assert(m.mainLine(id).find('\x1b') == std::string::npos);
    // Translation goes through the provider.
    Upper up;
    m.update(kFile, true, 1000);
    assert(m.mainLine(up) == "T:Profile: app:firefox  |  T:Joystick mode: timeline");
}

static ModeSet testModes() {
    ModeSet ms;
    for (const char *i : {"normal", "off", "timeline"}) {
        ModeDef d;
        d.id = i;
        d.label = std::string(i) == "timeline" ? "Timeline" : i;
        ms.modes.push_back(d);
    }
    return ms;
}

static void testTrayMenu() {
    IdentityStrings id;
    const ModeSet ms = testModes();
    StatusModel st;
    st.update(kFile, true, 1000);
    TrayMenuInput in;
    in.status = &st;
    in.modes = &ms;
    in.globalMode = "off";
    in.globalEnabled = false;
    TrayMenu m = buildTrayMenu(in, id);
    assert(m.entries.size() == 16);
    assert(m.entries[0].kind == TrayEntry::Kind::Info && m.entries[0].text == "Profile: app:firefox  |  Joystick mode: Timeline");
    assert(m.entries[1].visible && !m.entries[2].visible);
    const TrayEntry &mode = m.entries[4];
    assert(mode.kind == TrayEntry::Kind::Submenu && mode.children.size() == 3);
    assert(mode.children[0].arg == "normal" && !mode.children[0].checked);
    assert(mode.children[1].checked && mode.children[2].text == "Timeline (timeline)");
    assert(m.entries[5].kind == TrayEntry::Kind::Check && !m.entries[5].checked);
    assert(m.entries[7].action == TrayAction::OpenSettings && m.entries[11].action == TrayAction::ReleaseAllKeys && m.entries[15].action == TrayAction::Quit && m.entries[11].text == "Release all keys");
    assert(m.title == "dofscroll - Profile: app:firefox  |  Joystick mode: timeline");

    // No status, no modes.
    TrayMenu e = buildTrayMenu(TrayMenuInput(), id);
    assert(e.entries.size() == 16 && e.entries[4].children.empty());
    assert(e.entries[0].text == "Daemon not running / no status data" && !e.entries[1].visible);

    // Actions.
    RecordingActionRunner rr;
    std::string mset, err;
    int en = -1, opened = 0, quit = 0;
    TrayHooks h;
    h.configPath = "/c/dofscroll.conf";
    h.modesDir = "/c/modes";
    h.setMode = [&](const std::string &x) { mset = x; };
    h.setEnabled = [&](bool b) { en = b; };
    h.openSettings = [&] { ++opened; };
    h.quit = [&] { ++quit; };
    assert(performTrayAction(mode.children[2], rr, h, &err) && mset == "timeline");
    assert(performTrayAction(m.entries[5], rr, h, &err) && en == 1);  // was unchecked
    assert(performTrayAction(m.entries[7], rr, h, &err) && opened == 1);
    assert(performTrayAction(m.entries[15], rr, h, &err) && quit == 1);
    assert(performTrayAction(m.entries[9], rr, h, &err));  // showLog unset: skipped
    assert(performTrayAction(m.entries[8], rr, h, &err) && performTrayAction(m.entries[10], rr, h, &err));
    assert(rr.count(RecordingActionRunner::Kind::OpenPath) == 2);
    assert(performTrayAction(m.entries[11], rr, h, &err) && rr.count(RecordingActionRunner::Kind::ReleaseKeys) == 1);
    assert(rr.calls[0].path == "/c/dofscroll.conf" && rr.calls[1].path == "/c/modes");
    rr.result = false;
    assert(!performTrayAction(m.entries[8], rr, h, &err) && err == "failed");
}

static void testCurve() {
    ChannelCfg c;
    c.deadzone = 30;
    c.sensitivity = 5;
    c.curve_exp = 1.0;
    auto p = computeCurvePreview(c, 36, 350);
    assert(p.size() == 36 && p[0].input == 0 && p.back().input == 350);
    assert(p[0].curved == 0 && p[0].speed == 0);
    for (const auto &q : p) {
        const double v = applyCurve(c, applyDeadzone(q.input, c.deadzone));
        assert(q.curved == v && q.speed == v * 5 / 2500.0 * 125.0);
    }
    assert(previewMaxSpeed(p) == p.back().speed && previewMaxSpeed({}) == 0);
    c.sensitivity = 0;
    assert(previewMaxSpeed(computeCurvePreview(c)) == 0);
    c.sensitivity = 5;
    c.curve_exp = 2.0;
    auto q = computeCurvePreview(c, 64, 350);
    assert(q.back().curved < p.back().curved + 1e9 && q.back().curved > p.back().curved);  // convex above CURVE_REF
    c.curve_points = {{0, 0}, {100, 0.5}, {350, 3}};
    assert(computeCurvePreview(c).size() == 64);
    assert(computeCurvePreview(c, 1).empty() && computeCurvePreview(c, 10, 0).empty());
}

static void testRunner() {
    RecordingActionRunner r;
    std::string err;
    ActionRunner &a = r;
    assert(a.openPath("/x", &err) && a.setAutostart(true, &err) && a.requestReload(&err));
    assert(r.calls.size() == 3 && r.calls[1].enable && r.count(RecordingActionRunner::Kind::RequestReload) == 1);
    r.result = false;
    r.error = "nope";
    assert(!a.requestReload(&err) && err == "nope");
    assert(!a.requestReload(nullptr));
}

int main() {
    testStatus();
    testTrayMenu();
    testCurve();
    testRunner();
    puts("test_ui_models: ok");
    return 0;
}
