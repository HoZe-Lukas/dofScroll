// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Regression tests for "a key or scrolling gets stuck": every state transition while a key,
// a mouse button or a deflected axis is active must end with nothing held and no further
// wheel or key output once the cause is gone. Deterministic: fake input, fake clock, no
// threads (like tests/test_daemon_core.cpp).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "daemon_core.hpp"
#include "platform/fake_input.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// Records everything: what is held right now and how much output was produced.
struct Rec {
    std::set<int> keys;   // currently held keys
    std::set<int> mouse;  // currently held mouse buttons
    int wheels = 0;       // wheelV/wheelH calls
    int taps = 0;         // tapKey calls
    int releaseAlls = 0;
    bool held() const { return !keys.empty() || !mouse.empty(); }
    int output() const { return wheels + taps; }
};

struct RecOutput : Output {
    Rec *r;
    explicit RecOutput(Rec *rec) : r(rec) {}
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { if (n) ++r->wheels; }
    void wheelH(int n) override { if (n) ++r->wheels; }
    void setKey(Key k, bool d) override {
        if (d) r->keys.insert(static_cast<int>(k)); else r->keys.erase(static_cast<int>(k));
    }
    void tapKey(Key) override { ++r->taps; }
    void mouseButton(int b, bool d) override {
        if (d) r->mouse.insert(b); else r->mouse.erase(b);
    }
    void releaseAll() override { r->keys.clear(); r->mouse.clear(); ++r->releaseAlls; }
    void flush() override {}
};

struct FakeWindow : ActiveWindowProvider {
    std::string cls;
    bool init() override { return true; }
    std::string poll() override { return cls; }
};

struct FakeReload : ReloadListener {
    bool pending = false;
    bool init(std::string *) override { return true; }
    bool pollReload() override { bool p = pending; pending = false; return p; }
};

struct TestSource : FakeInputSource {
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> devices() const override { return list; }
};

static void writeFile(const std::string &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    f << text;
}

struct Rig {
    std::string dir, conf;
    Rec rec;
    TestSource src;
    FakeWindow win;
    FakeReload reload;
    bool suspend = false;
    int64_t now = 10'000'000'000;
    std::unique_ptr<DaemonCore> core;

    explicit Rig(const std::string &config,
                 const std::vector<std::pair<std::string, std::string>> &modeFiles = {})
        : dir(tu::makeTempDir("dofscroll-stuck-test")) {
        conf = dir + "/dofscroll.conf";
        writeFile(conf, config);
        if (!modeFiles.empty()) {
            std::filesystem::create_directory(dir + "/modes");
            for (const auto &f : modeFiles) writeFile(dir + "/modes/" + f.first, f.second);
        }
        DeviceInfo di;
        di.name = "Test Mouse A";
        di.reportedButtons = 3;
        src.list = {di};
        DaemonDeps deps;
        deps.input = &src;
        deps.activeWindow = &win;
        deps.reload = &reload;
        deps.paths.configDir = dir;
        deps.paths.runtimeDir = dir;
        Rec *rp = &rec;
        deps.makeOutput = [rp](const std::string &, std::string *) {
            return std::unique_ptr<Output>(new RecOutput(rp));
        };
        deps.clock = [this] { return now; };
        deps.suspendRequested = [this] { return suspend; };
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~Rig() { tu::removeAll(dir); }

    void pump() {
        InputEvent e;
        while (src.wait(&e, 0) == WaitResult::Event) core->reader().handle(e);
    }
    void run(double secs) {
        const int n = static_cast<int>(secs * 250);
        for (int i = 0; i < n; ++i) {
            now += 4'000'000;
            pump();
            core->updateProfile(now);
            core->tickOnce(now, 0.004);
        }
    }
    void move() { src.pushMotion(0, 69, 286, 350, 145, 56, 155); }  // vertical push
    void stop() { src.pushMotion(0, 0, 0, 0, 0, 0, 0); }
    void devicesChanged() {
        InputEvent e;
        e.type = InputEventType::DevicesChanged;
        src.push(e);
    }
    // Nothing may be held and no output may follow: the cause (axis, button) is gone.
    void expectQuiet(const char *what, double grace = 0.6) {
        run(grace);
        const int before = rec.output();
        run(0.5);
        CHECK(!rec.held(), "%s: still held (%zu keys, %zu mouse)", what, rec.keys.size(), rec.mouse.size());
        CHECK(rec.output() == before, "%s: output continued (%d events)", what, rec.output() - before);
    }
};

static const int KEY_UP = static_cast<int>(Key::Up);

static const char *const NAV_CONF =
    "[global]\nTICK_HZ = 250\nNAV_BUTTON = 0\nNAV_BUTTON_ENABLED = 0\n"
    "[device:Test Mouse A]\nNAV_BUTTON_ENABLED = 1\n"
    "[app:plain]\nNAV_BUTTON_ENABLED = 0\n";

static const char *const KEYBIND_CONF = "[global]\nTICK_HZ = 250\nBUTTON_1 = key:up\n";

// --- device list changes --------------------------------------------------------------
static void testDevicesChangedMidMotion() {
    Rig r(NAV_CONF);
    r.move();
    r.run(0.3);
    CHECK(r.rec.output() > 0, "motion must produce output");
    r.devicesChanged();
    r.src.list.clear();  // device info unknown afterwards
    r.run(0.05);
    r.expectQuiet("devices changed mid motion");
    CHECK(r.core->deviceName().empty(), "device name unknown");
}

static void testDevicesChangedMidButtonHold() {
    Rig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    CHECK(r.rec.keys.count(KEY_UP), "button must hold the key");
    r.devicesChanged();
    r.src.list.clear();
    r.expectQuiet("devices changed mid button hold");
    // The late release of the (physically still held) button must not matter.
    r.src.pushButton(0, 0, false);
    r.expectQuiet("late release after device change");
}

static void testDevicesChangedMidNavHold() {
    Rig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.3);
    CHECK(r.rec.taps > 0, "nav must tap arrows");
    r.devicesChanged();
    r.src.list.clear();
    r.src.pushButton(0, 0, false);
    r.expectQuiet("devices changed mid nav hold");
}

// --- profile / program changes ----------------------------------------------------------
static void testProgramChangeMidNavHold() {
    Rig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.2);
    CHECK(r.rec.taps > 0, "nav taps expected");
    r.win.cls = "plain";  // profile without nav: scrolling instead of arrows
    r.run(0.1);
    const int taps_after = r.rec.taps;
    r.run(0.1);
    CHECK(r.rec.taps == taps_after, "no arrow taps after the profile lost NAV");
    r.src.pushButton(0, 0, false);
    r.stop();
    r.expectQuiet("program change mid nav hold");
}

static void testProgramChangeMidKeyHold() {
    Rig r("[global]\nBUTTON_1 = key:up\n[app:plain]\nBUTTON_1 = none\n");
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    CHECK(r.rec.keys.count(KEY_UP), "key held");
    r.win.cls = "plain";
    r.run(0.3);
    CHECK(!r.rec.keys.count(KEY_UP), "key must be released when the new profile unbinds the button");
}

// --- suspend heartbeat --------------------------------------------------------------------
static void testSuspendMidMotionAndResume() {
    Rig r(NAV_CONF);
    r.move();
    r.run(0.2);
    r.suspend = true;
    r.run(0.2);
    const int before = r.rec.output();
    r.run(0.2);
    CHECK(r.rec.output() == before, "no output while suspended");
    r.suspend = false;  // heartbeat vanishes; the source stayed silent
    r.expectQuiet("resume after suspend, silent source");
}

static void testSuspendMidButtonHold() {
    Rig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.suspend = true;
    r.run(0.2);
    CHECK(!r.rec.held(), "suspend releases the key");
    r.src.pushButton(0, 0, false);  // released while suspended
    r.run(0.1);
    r.suspend = false;
    r.expectQuiet("resume after release during suspend");
}

// --- config reload / modes ------------------------------------------------------------------
static void testReloadMidHold() {
    Rig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    CHECK(r.rec.keys.count(KEY_UP), "key held");
    writeFile(r.conf, "[global]\nTICK_HZ = 250\n");  // binding removed while held
    r.reload.pending = true;
    r.run(0.3);
    CHECK(!r.rec.held(), "reload without the binding must release the key");
    r.src.pushButton(0, 0, false);
    r.expectQuiet("reload mid hold");
}

static void testModeSwitchMidHold() {
    Rig r("[global]\nTICK_HZ = 250\nMODE = alpha\n",
          {{"alpha.conf", "BUTTON_1 = key:up\nBUTTON_2 = mode:next\n"},
           {"beta.conf", "BUTTON_2 = mode:next\n"}});
    r.run(0.1);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    CHECK(r.rec.keys.count(KEY_UP), "alpha holds the key (mode %s)", r.core->runner().mode().id.c_str());
    r.src.pushButton(0, 1, true);  // mode:next while the first button is still down
    r.run(0.2);
    CHECK(!r.rec.keys.count(KEY_UP), "mode switch must release the key (mode %s)",
          r.core->runner().mode().id.c_str());
    r.src.pushButton(0, 1, false);
    r.src.pushButton(0, 0, false);
    r.expectQuiet("mode switch mid hold");
}

static void testEnabledToggle() {
    Rig r("[global]\nTICK_HZ = 250\nBUTTON_1 = key:up\n[app:off]\nENABLED = 0\n");
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.15);
    CHECK(r.rec.keys.count(KEY_UP), "key held");
    r.win.cls = "off";
    r.run(0.3);
    CHECK(!r.rec.held(), "ENABLED=0 releases everything");
    r.win.cls = "";
    r.src.pushButton(0, 0, false);
    r.stop();
    r.expectQuiet("ENABLED toggled back");
}

// --- nav button / custom channels / stale watchdog -----------------------------------------------
static void testNavButtonReleasedMidMotion() {
    Rig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.2);
    CHECK(r.rec.taps > 0 && r.rec.wheels == 0, "nav: arrows, no wheel");
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    r.rec.taps = 0;
    r.move();
    r.run(0.2);
    CHECK(r.rec.taps == 0 && r.rec.wheels > 0, "after the nav button is released: wheel, no arrows");
    r.stop();
    r.expectQuiet("nav button released");
}

static void testCustomChannelStaysDeflected() {
    Rig r("[global]\nTICK_HZ = 250\nMODE = keysmode\n",
          {{"keysmode.conf", "V_ACTION = keys:up,down\nH_ACTION = keys:left,right\n"}});
    r.run(0.1);
    r.move();
    r.run(0.2);
    CHECK(r.rec.taps > 0, "keys channel must tap (mode %s)", r.core->runner().mode().id.c_str());
    // The source goes silent while the axis is deflected: output must stop within the
    // documented time (stale watchdog 250 ms).
    r.run(0.3);
    const int before = r.rec.taps;
    r.run(0.5);
    CHECK(r.rec.taps == before, "keys channel kept tapping with a silent source");
    CHECK(!r.rec.held(), "nothing held");
}

static void testStaleWatchdogWithNav() {
    Rig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.3);
    r.run(0.3);
    const int before = r.rec.output();
    r.run(0.5);
    CHECK(r.rec.output() == before, "nav output must stop when the source goes silent");
}

static void testZoomCtrlSilentSource() {
    Rig r("[global]\nTICK_HZ = 250\n");
    r.src.pushMotion(0, 82, 144, 90, 35, 350, 111);  // twist
    r.run(0.2);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "zoom holds Ctrl");
    r.run(0.6);  // source silent
    CHECK(!r.rec.held(), "Ctrl must be released when the source goes silent");
}

// --- safety net --------------------------------------------------------------------------------
// The output desynchronises from the runner (a lost key-up): the safety net must clear it.
static void testSafetyNetReleasesOutput() {
    Rig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.rec.keys.insert(static_cast<int>(Key::Ctrl));  // a key the output holds but the runner does not know
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    CHECK(!r.rec.keys.count(KEY_UP), "normal release");
    const int before = r.rec.releaseAlls;
    r.expectQuiet("safety net after output desync", 0.5);
    CHECK(r.rec.releaseAlls > before, "safety net must call Output::releaseAll");
}

// A held button is activity, however long it lasts: the net must not break click-and-hold.
static void testSafetyNetKeepsLegitimateHold() {
    Rig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(2.0);
    CHECK(r.rec.keys.count(KEY_UP), "legitimate hold must survive the safety net");
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    CHECK(!r.rec.held(), "released with the button");
}

int main() {
    testDevicesChangedMidMotion();
    testDevicesChangedMidButtonHold();
    testDevicesChangedMidNavHold();
    testProgramChangeMidNavHold();
    testProgramChangeMidKeyHold();
    testSuspendMidMotionAndResume();
    testSuspendMidButtonHold();
    testReloadMidHold();
    testModeSwitchMidHold();
    testEnabledToggle();
    testNavButtonReleasedMidMotion();
    testCustomChannelStaysDeflected();
    testStaleWatchdogWithNav();
    testZoomCtrlSilentSource();
    testSafetyNetReleasesOutput();
    testSafetyNetKeepsLegitimateHold();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_stuck_keys: all passed");
    return 0;
}
