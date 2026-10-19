// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/app_picker_model.hpp (assert-based, own main).
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/model/app_picker_model.hpp"

using namespace spn;

static RunningApp app(const char *match, const char *name, const char *title) {
    RunningApp a;
    a.matchString = match;
    a.displayName = name;
    a.windowTitle = title;
    return a;
}

static void testFilterSortFlag() {
    AppPickerModel m;
    m.setApps({app("navigator firefox", "Firefox", "Docs - Mozilla"),
               app("code code", "Code", "main.cpp"),
               app("alacritty alacritty", "Alacritty", "shell"),
               app("gimp-2.10 gimp", "Gimp", "Image")});
    m.setConfigured({"Code Code", "unrelated"});
    auto all = m.rows("");
    assert(all.size() == 4);
    assert(all[0].app.displayName == "Alacritty" && all[3].app.displayName == "Gimp");
    assert(all[1].app.displayName == "Code" && all[1].hasProfile);  // flagged, not hidden
    assert(!all[0].hasProfile);
    assert(m.rows("MOZILLA").size() == 1);   // title, case-insensitive
    assert(m.rows("gimp-2").size() == 1);    // class
    assert(m.rows("fire")[0].app.displayName == "Firefox");  // name
    assert(m.rows("zzz").empty());
}

static void testStableSort() {
    AppPickerModel m;
    m.setApps({app("a1", "Same", "first"), app("a2", "same", "second"), app("a3", "Aaa", "x")});
    auto r = m.rows("");
    assert(r[0].app.matchString == "a3");
    assert(r[1].app.matchString == "a1" && r[2].app.matchString == "a2");
}

static void testValidity() {
    std::string n, e;
    assert(validateProfilePattern("  firefox  ", &n, &e) && n == "firefox");
    assert(!validateProfilePattern("", &n, &e) && !e.empty());
    assert(!validateProfilePattern("   ", &n, &e));
    assert(validateProfilePattern("a]b", &n, &e) && n == "a]b");  // brackets are fine in names
    assert(!validateProfilePattern("a\x01" "b", &n, &e) && !e.empty());  // control character
    assert(!validateProfilePattern("a\nb", &n, nullptr));
}

static void testCapture() {
    CaptureNextFocus c;
    assert(c.state() == CaptureNextFocus::State::Idle);
    assert(!c.tick("x", 0));  // idle: nothing
    c.start(1000, "dofscroll-ui");
    assert(!c.tick("", 1100));                       // empty ignored
    assert(!c.tick("dofscroll-ui dofscroll-ui", 1200));  // own window ignored
    assert(!c.tick("dofscroll-ui", 1300));           // baseline
    assert(c.remainingMs(1300) == 7700);
    assert(c.tick("navigator firefox", 1400));
    assert(c.state() == CaptureNextFocus::State::Captured);
    assert(c.result() == "navigator firefox");
    assert(!c.tick("code code", 1500));              // only once
    assert(c.result() == "navigator firefox");

    c.start(0, "term");
    assert(!c.tick("term", 7999));
    assert(!c.tick("code code", 8000));              // timeout wins at the boundary
    assert(c.state() == CaptureNextFocus::State::TimedOut);
    assert(c.result().empty());

    CaptureNextFocus s(100);
    s.start(0);
    assert(s.tick("a b", 99));
    s.start(0);
    s.cancel();
    assert(!s.tick("a b", 1) && s.state() == CaptureNextFocus::State::Idle);
}

// Drives the state machine the way the dialog does (baseline = provider at start, then a 100 ms
// timer) with a fake ActiveWindowProvider that returns Windows-style executable names.
namespace {
struct FakeProvider : ActiveWindowProvider {
    std::vector<std::string> script;
    size_t pos = 0;
    bool init() override { return true; }
    std::string poll() override { return pos < script.size() ? script[pos++] : script.empty() ? "" : script.back(); }
};
}  // namespace

static void testCaptureWithProvider() {
    FakeProvider p;
    p.script = {"dofscroll-ui.exe", "dofscroll-ui.exe", "", "explorer.exe", "caf\xC3\xA9.exe"};
    CaptureNextFocus c;
    int64_t now = 0;
    c.start(now, p.poll());  // baseline: the UI itself
    int ticks = 0;
    while (c.state() == CaptureNextFocus::State::Waiting && ticks < 50) {
        now += 100;
        c.tick(p.poll(), now);
        ticks++;
    }
    assert(c.state() == CaptureNextFocus::State::Captured && c.result() == "explorer.exe" && ticks == 3);

    // nothing but the UI and empty strings (e.g. an elevated program denies the query): timeout
    FakeProvider q;
    q.script = {"dofscroll-ui.exe", "", "dofscroll-ui.exe"};
    CaptureNextFocus d(1000);
    d.start(0, q.poll());
    for (int64_t t = 100; t <= 1000; t += 100) d.tick(q.poll(), t);
    assert(d.state() == CaptureNextFocus::State::TimedOut && d.result().empty());

    // Unicode executable name is captured unchanged (same string the profile matcher compares)
    FakeProvider u;
    u.script = {"x.exe", "caf\xC3\xA9.exe"};
    CaptureNextFocus e;
    e.start(0, u.poll());
    assert(e.tick(u.poll(), 100) && e.result() == "caf\xC3\xA9.exe");
}

static void testDevices() {
    DevicePickerModel m;
    DeviceInfo a, b, c;
    a.name = "SpaceMouse Pro";
    a.vendor = 0x256f;
    a.product = 0xc62b;
    b.name = "Another Device";
    c.name = "SpaceNavigator";
    c.vendor = 0x46d;
    c.product = 0xc626;
    m.setDevices({a, b, c});
    m.setConfigured({"spacemouse pro"});
    auto r = m.rows("");
    assert(r.size() == 3 && r[0].dev.name == "Another Device");
    assert(r[0].idString.empty() && !r[0].hasProfile);
    assert(r[2].dev.name == "SpaceNavigator" && r[2].idString == "046d:c626");
    assert(r[1].hasProfile && r[1].idString == "256f:c62b");
    assert(m.rows("C62B").size() == 1);
    assert(m.rows("space").size() == 2);
}

int main() {
    testFilterSortFlag();
    testStableSort();
    testValidity();
    testCapture();
    testCaptureWithProvider();
    testDevices();
    std::puts("test_app_picker: all tests passed");
    return 0;
}
