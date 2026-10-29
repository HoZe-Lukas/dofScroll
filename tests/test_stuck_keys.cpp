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
struct StuckRec {
    std::set<int> keys;   // currently held keys
    std::set<int> mouse;  // currently held mouse buttons
    int wheels = 0;       // wheelV/wheelH calls
    int taps = 0;         // tapKey calls
    int clicks = 0;       // mouse buttons going down
    int releaseAlls = 0;
    bool held() const { return !keys.empty() || !mouse.empty(); }
    int output() const { return wheels + taps; }
};

struct StuckOutput : Output {
    StuckRec *r;
    explicit StuckOutput(StuckRec *rec) : r(rec) {}
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { if (n) ++r->wheels; }
    void wheelH(int n) override { if (n) ++r->wheels; }
    void setKey(Key k, bool d) override {
        if (d) r->keys.insert(static_cast<int>(k)); else r->keys.erase(static_cast<int>(k));
    }
    void tapKey(Key) override { ++r->taps; }
    void mouseButton(int b, bool d) override {
        if (d) { r->mouse.insert(b); ++r->clicks; } else r->mouse.erase(b);
    }
    void releaseAll() override { r->keys.clear(); r->mouse.clear(); ++r->releaseAlls; }
    void flush() override {}
};

struct StuckWindow : ActiveWindowProvider {
    std::string cls;
    bool init() override { return true; }
    std::string poll() override { return cls; }
};

struct StuckReload : ReloadListener {
    bool pending = false;
    bool init(std::string *) override { return true; }
    bool pollReload() override { bool p = pending; pending = false; return p; }
};

struct StuckSource : FakeInputSource {
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> devices() const override { return list; }
};

static void writeFile(const std::string &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    f << text;
}

struct StuckRig {
    std::string dir, conf;
    StuckRec rec;
    StuckSource src;
    StuckWindow win;
    StuckReload reload;
    bool suspend = false;
    int64_t now = 10'000'000'000;
    std::unique_ptr<DaemonCore> core;

    explicit StuckRig(const std::string &config,
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
        StuckRec *rp = &rec;
        deps.makeOutput = [rp](const std::string &, std::string *) {
            return std::unique_ptr<Output>(new StuckOutput(rp));
        };
        deps.clock = [this] { return now; };
        deps.suspendRequested = [this] { return suspend; };
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~StuckRig() { tu::removeAll(dir); }

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
    StuckRig r(NAV_CONF);
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
    StuckRig r(KEYBIND_CONF);
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
    StuckRig r(NAV_CONF);
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
    StuckRig r(NAV_CONF);
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
    StuckRig r("[global]\nBUTTON_1 = key:up\n[app:plain]\nBUTTON_1 = none\n");
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    CHECK(r.rec.keys.count(KEY_UP), "key held");
    r.win.cls = "plain";
    r.run(0.3);
    CHECK(!r.rec.keys.count(KEY_UP), "key must be released when the new profile unbinds the button");
}

// --- suspend heartbeat --------------------------------------------------------------------
static void testSuspendMidMotionAndResume() {
    StuckRig r(NAV_CONF);
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
    StuckRig r(KEYBIND_CONF);
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
    StuckRig r(KEYBIND_CONF);
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
    StuckRig r("[global]\nTICK_HZ = 250\nMODE = alpha\n",
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
    StuckRig r("[global]\nTICK_HZ = 250\nBUTTON_1 = key:up\n[app:off]\nENABLED = 0\n");
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
    StuckRig r(NAV_CONF);
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
    StuckRig r("[global]\nTICK_HZ = 250\nMODE = keysmode\n",
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
    StuckRig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.3);
    r.run(0.3);
    const int before = r.rec.output();
    r.run(0.5);
    CHECK(r.rec.output() == before, "nav output must stop when the source goes silent");
}

static void testZoomCtrlSilentSource() {
    StuckRig r("[global]\nTICK_HZ = 250\n");
    r.src.pushMotion(0, 82, 144, 90, 35, 350, 111);  // twist
    r.run(0.2);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "zoom holds Ctrl");
    r.run(0.6);  // source silent
    CHECK(!r.rec.held(), "Ctrl must be released when the source goes silent");
}

// --- safety net --------------------------------------------------------------------------------
// The output desynchronises from the runner (a lost key-up): the safety net must clear it.
static void testSafetyNetReleasesOutput() {
    StuckRig r(KEYBIND_CONF);
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
    StuckRig r(KEYBIND_CONF);
    r.src.pushButton(0, 0, true);
    r.run(2.0);
    CHECK(r.rec.keys.count(KEY_UP), "legitimate hold must survive the safety net");
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    CHECK(!r.rec.held(), "released with the button");
}

// --- split push and tilt (a part of a split channel holds Ctrl for zoom) --------------------------
static const char *const SPLIT_ZOOM_MODE =
    "V_PUSH_ACTION = wheel_v\nV_TILT_ACTION = zoom\nBUTTON_2 = mode:next\n";
static const char *const SPLIT_CONF = "[global]\nTICK_HZ = 250\nMODE = splitz\n";
static void tiltForward(StuckRig &r) { r.src.pushMotion(0, 0, 0, 0, 300, 0, 0); }  // tilt forward: rx

static void testSplitPartZoomHoldsCtrl() {
    StuckRig r(SPLIT_CONF, {{"splitz.conf", SPLIT_ZOOM_MODE}});
    r.run(0.1);
    tiltForward(r);
    r.run(0.15);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)) && r.rec.wheels > 0,
          "the tilt part zooms: Ctrl held (mode %s)", r.core->runner().mode().id.c_str());
    r.stop();
    r.expectQuiet("split zoom part released");
}

static void testSplitPartSilentSource() {
    StuckRig r(SPLIT_CONF, {{"splitz.conf", SPLIT_ZOOM_MODE}});
    r.run(0.1);
    tiltForward(r);
    r.run(0.15);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "Ctrl held");
    r.run(0.6);  // source silent: the stale watchdog zeroes the axes
    CHECK(!r.rec.held(), "Ctrl must be released when the source goes silent");
}

static void testSplitPartDeviceUnplugged() {
    StuckRig r(SPLIT_CONF, {{"splitz.conf", SPLIT_ZOOM_MODE}});
    r.run(0.1);
    tiltForward(r);
    r.run(0.15);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "Ctrl held");
    r.devicesChanged();
    r.src.list.clear();
    r.run(0.05);
    r.expectQuiet("device unplugged while a split part zooms");
}

static void testSplitPartModeSwitchMidMotion() {
    StuckRig r(SPLIT_CONF, {{"splitz.conf", SPLIT_ZOOM_MODE}, {"zzz.conf", "BUTTON_2 = mode:next\n"}});
    r.run(0.1);
    tiltForward(r);
    r.run(0.15);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "Ctrl held");
    r.src.pushButton(0, 1, true);  // mode:next while the tilt is deflected
    r.run(0.1);
    CHECK(!r.rec.keys.count(static_cast<int>(Key::Ctrl)), "mode switch releases Ctrl (mode %s)",
          r.core->runner().mode().id.c_str());
    r.src.pushButton(0, 1, false);
    r.stop();
    r.expectQuiet("mode switch while a split part zooms");
}

static void testSplitPartReloadMidMotion() {
    StuckRig r(SPLIT_CONF, {{"splitz.conf", SPLIT_ZOOM_MODE}});
    r.run(0.1);
    tiltForward(r);
    r.run(0.15);
    CHECK(r.rec.keys.count(static_cast<int>(Key::Ctrl)), "Ctrl held");
    r.reload.pending = true;
    r.stop();
    r.run(0.1);
    r.expectQuiet("reload while a split part zooms");
}

// --- every button action x every way the cause can disappear ----------------------------------------
// Each action that holds something while the button is down (key, modifier key, the three mouse buttons) or taps
// (chord) must leave nothing held when the binding goes away through a reload, ENABLED = 0, a program profile that
// unbinds the button, a suspend, an unplugged device or a joystick mode switch.
struct ActionCase {
    const char *binding;
    bool holds;  // something stays pressed while the button is down (a chord is tapped once)
};
static const ActionCase kActions[] = {
    {"key:up", true},    {"key:shift", true},      {"key:ctrl", true},       {"key:alt", true},
    {"key:super", true}, {"click", true},          {"rightclick", true},     {"middleclick", true},
    {"key:ctrl+0", false}, {"key:ctrl+shift+t", false},
};

static std::string actionConf(const char *binding) {
    return std::string("[global]\nTICK_HZ = 250\nBUTTON_1 = ") + binding +
           "\n[app:off]\nENABLED = 0\n[app:unb]\nBUTTON_1 = none\n";
}

static void checkHeld(StuckRig &r, const ActionCase &a, const char *when) {
    if (a.holds) CHECK(r.rec.held(), "%s: %s must be held while the button is down", when, a.binding);
}

static void testEveryActionEveryTransition() {
    for (const ActionCase &a : kActions) {
        for (int tr = 0; tr < 6; ++tr) {
            static const char *const names[] = {"reload", "ENABLED=0", "program unbinds", "suspend", "unplugged", "mode switch"};
            char what[160];
            std::snprintf(what, sizeof what, "%s / %s", a.binding, names[tr]);
            const bool viaMode = tr == 5;
            StuckRig r(viaMode ? "[global]\nTICK_HZ = 250\nMODE = alpha\n" : actionConf(a.binding),
                       viaMode ? std::vector<std::pair<std::string, std::string>>{
                                     {"alpha.conf", std::string("BUTTON_1 = ") + a.binding + "\nBUTTON_2 = mode:next\n"},
                                     {"beta.conf", "BUTTON_2 = mode:next\n"}}
                               : std::vector<std::pair<std::string, std::string>>{});
            r.run(0.1);
            r.src.pushButton(0, 0, true);
            r.run(0.15);
            checkHeld(r, a, what);
            switch (tr) {
            case 0:
                writeFile(r.conf, "[global]\nTICK_HZ = 250\n");
                r.reload.pending = true;
                break;
            case 1: r.win.cls = "off"; break;
            case 2: r.win.cls = "unb"; break;
            case 3: r.suspend = true; break;
            case 4:
                r.devicesChanged();
                r.src.list.clear();
                break;
            case 5: r.src.pushButton(0, 1, true); break;
            }
            r.run(0.3);
            CHECK(!r.rec.held(), "%s: still held after the transition (%zu keys, %zu mouse)", what, r.rec.keys.size(),
                  r.rec.mouse.size());
            if (viaMode) r.src.pushButton(0, 1, false);
            r.src.pushButton(0, 0, false);
            r.run(0.1);
            if (tr == 3) r.suspend = false;
            r.win.cls = "";
            r.expectQuiet(what);
        }
    }
}

// The zoom modifier keys (ZOOM_MODIFIER, one to three keys) are released by every transition, not only by a silent source.
static void testZoomModifiersEveryTransition() {
    static const char *const mods[] = {"ctrl", "shift", "alt", "super", "ctrl+shift", "ctrl+alt+shift"};
    for (const char *m : mods) {
        for (int tr = 0; tr < 6; ++tr) {
            static const char *const names[] = {"reload", "ENABLED=0", "ZOOM_MODIFIER none", "suspend", "unplugged", "mode switch"};
            char what[160];
            std::snprintf(what, sizeof what, "zoom %s / %s", m, names[tr]);
            const std::string conf = std::string("[global]\nTICK_HZ = 250\nZOOM_MODIFIER = ") + m +
                                     "\n[app:off]\nENABLED = 0\n[app:nomod]\nZOOM_MODIFIER = none\n";
            StuckRig r(tr == 5 ? std::string("[global]\nTICK_HZ = 250\nMODE = alpha\nZOOM_MODIFIER = ") + m + "\n" : conf,
                       tr == 5 ? std::vector<std::pair<std::string, std::string>>{{"alpha.conf", "BUTTON_2 = mode:next\n"},
                                                                                  {"beta.conf", "BUTTON_2 = mode:next\nZOOM_SENS = 0\n"}}
                               : std::vector<std::pair<std::string, std::string>>{});
            r.run(0.1);
            r.src.pushMotion(0, 82, 144, 90, 35, 350, 111);  // twist
            r.run(0.2);
            CHECK(r.rec.keys.size() >= 1 && r.rec.wheels > 0, "%s: zoom must hold its modifier", what);
            switch (tr) {
            case 0:
                writeFile(r.conf, "[global]\nTICK_HZ = 250\nZOOM_MODIFIER = none\n");
                r.reload.pending = true;
                break;
            case 1: r.win.cls = "off"; break;
            case 2: r.win.cls = "nomod"; break;
            case 3: r.suspend = true; break;
            case 4:
                r.devicesChanged();
                r.src.list.clear();
                break;
            case 5: r.src.pushButton(0, 1, true); break;
            }
            r.run(0.3);
            CHECK(!r.rec.held(), "%s: modifier still held (%zu keys)", what, r.rec.keys.size());
            if (tr == 5) r.src.pushButton(0, 1, false);
            r.stop();
            if (tr == 3) r.suspend = false;
            r.win.cls = "";
            r.expectQuiet(what);
        }
    }
}

// The toggle:nav latch must not survive what the header of ModeRunner::clearToggles lists (reload, disabled, suspend).
// After each, scrolling is back to the wheel and no arrow keys are tapped.
static void testNavLatchCleared() {
    static const char *const names[] = {"reload", "ENABLED=0", "suspend"};
    for (int tr = 0; tr < 3; ++tr) {
        StuckRig r("[global]\nTICK_HZ = 250\nBUTTON_1 = toggle:nav\n[app:off]\nENABLED = 0\n");
        r.run(0.1);
        r.src.pushButton(0, 0, true);
        r.run(0.05);
        r.src.pushButton(0, 0, false);
        r.run(0.05);
        r.move();
        r.run(0.2);
        CHECK(r.rec.taps > 0 && r.rec.wheels == 0, "%s: latched nav taps arrows", names[tr]);
        r.stop();
        r.run(0.1);
        switch (tr) {
        case 0: r.reload.pending = true; break;
        case 1: r.win.cls = "off"; break;
        case 2: r.suspend = true; break;
        }
        r.run(0.3);
        if (tr == 1) r.win.cls = "";
        if (tr == 2) r.suspend = false;
        r.run(0.3);
        r.rec.taps = r.rec.wheels = 0;
        r.move();
        r.run(0.2);
        CHECK(r.rec.taps == 0 && r.rec.wheels > 0, "%s: the nav latch must be cleared (taps %d, wheels %d)", names[tr], r.rec.taps,
              r.rec.wheels);
        r.stop();
        r.expectQuiet(names[tr]);
    }
}

// --- the key engine (stage 3 of #224): analog and digital keys, chords with modifiers, rate limits and triggers -----------------
// A key function taps atomically (a chord presses and releases its modifiers inside the same call), so nothing may ever be
// left held, and once the cause is gone (reload, ENABLED = 0, a program that unbinds the motion, suspend, an unplugged
// device, a joystick mode switch) no key may be sent any more while the cap is still deflected.
struct EngineCase {
    const char *action;  // lines of the profile
    const char *name;
};
static const EngineCase kEngineCases[] = {
    {"V_ACTION = keys:ctrl+pageup,ctrl+pagedown\n", "analog chord"},
    {"V_ACTION = keys:up,down\nSCROLL_V_PUSH_KEY_RATE_MIN = 3\n", "analog with a lower limit"},
    {"V_ACTION = keys:up,down\nSCROLL_V_PUSH_TRIGGER = 15\nSCROLL_V_PUSH_KEY_RATE_MAX = 20\n", "analog with a trigger"},
    {"V_ACTION = step:alt+left,alt+right\n", "digital alt chord"},
    {"V_ACTION = step:shift,ctrl\nSCROLL_V_PUSH_TRIGGER = 5\n", "digital modifier keys"},
    {"V_PUSH_ACTION = keys:none,down\nV_TILT_ACTION = step:up,none\n", "none per side, split"},
};

static void testKeyEngineEveryTransition() {
    static const char *const names[] = {"reload", "ENABLED=0", "program unbinds", "suspend", "unplugged", "mode switch"};
    for (const EngineCase &a : kEngineCases) {
        for (int tr = 0; tr < 6; ++tr) {
            char what[200];
            std::snprintf(what, sizeof what, "%s / %s", a.name, names[tr]);
            const std::string lines = a.action;
            std::string modeText = lines;  // the same keys in a joystick mode file (V_ACTION etc. are mode keys too)
            const bool viaMode = tr == 5;
            StuckRig r(viaMode ? std::string("[global]\nTICK_HZ = 250\nMODE = alpha\n")
                               : "[global]\nTICK_HZ = 250\n" + lines + "[app:off]\nENABLED = 0\n[app:unb]\nV_ACTION = none\nV_PUSH_ACTION = none\nV_TILT_ACTION = none\n",
                       viaMode ? std::vector<std::pair<std::string, std::string>>{{"alpha.conf", modeText + "BUTTON_2 = mode:next\n"},
                                                                                  {"beta.conf", "V_ACTION = none\nBUTTON_2 = mode:next\n"}}
                               : std::vector<std::pair<std::string, std::string>>{});
            r.run(0.1);
            r.move();
            r.run(0.3);
            CHECK(!r.rec.held(), "%s: a tap must never stay held", what);
            CHECK(r.rec.taps > 0, "%s: the function presses while the cap is deflected", what);
            switch (tr) {
            case 0:
                writeFile(r.conf, "[global]\nTICK_HZ = 250\nV_ACTION = none\nV_PUSH_ACTION = none\nV_TILT_ACTION = none\n");
                r.reload.pending = true;
                break;
            case 1: r.win.cls = "off"; break;
            case 2: r.win.cls = "unb"; break;
            case 3: r.suspend = true; break;
            case 4:
                r.devicesChanged();
                r.src.list.clear();
                break;
            case 5: r.src.pushButton(0, 1, true); break;
            }
            r.run(0.3);
            CHECK(!r.rec.held(), "%s: still held after the transition (%zu keys)", what, r.rec.keys.size());
            const int before = r.rec.taps;
            r.run(0.5);  // the cap is still deflected: the cause is gone, so no key may follow
            CHECK(r.rec.taps == before, "%s: %d keys sent after the transition", what, r.rec.taps - before);
            if (tr == 5) r.src.pushButton(0, 1, false);
            r.stop();
            if (tr == 3) r.suspend = false;
            r.win.cls = "";
            r.expectQuiet(what);
        }
    }
}

// The lift (y) with a digital key (the only key function it takes): the same release paths, and the safety net.
static void testKeyEngineLift() {
    StuckRig r("[global]\nTICK_HZ = 250\nLIFT_ACTION = step:volume_up,volume_down\nLIFT_DEADZONE = 20\n[app:off]\nENABLED = 0\n");
    r.run(0.1);
    r.src.pushMotion(0, 0, 300, 0, 0, 0, 0);
    r.run(0.3);
    CHECK(r.rec.taps == 1 && !r.rec.held(), "lift: one press per push (%d), nothing held", r.rec.taps);
    r.win.cls = "off";
    r.run(0.3);
    r.src.pushMotion(0, 0, 0, 0, 0, 0, 0);
    r.src.pushMotion(0, 0, -300, 0, 0, 0, 0);
    r.run(0.3);
    CHECK(r.rec.taps == 1 && !r.rec.held(), "lift: nothing while the program disables control");
    r.stop();
    r.win.cls = "";
    r.expectQuiet("lift digital");
}


// --- mouse clicks and holds on every motion (stage 4 of #224) -------------------------------------------------------------
// hold:<neg>,<pos> keeps a key, chord or mouse button down while the motion is deflected, step: with click presses and releases a
// button. Every way the cause can disappear (reload, ENABLED = 0, a program that unbinds the motion, suspend, an unplugged device,
// a joystick mode switch, a silent source) must leave nothing held, and no click may follow while the cap is still deflected.
struct MotionCase {
    const char *name;
    const char *key;  // action key of the motion
    int x, y, z, rx, ry, rz;
    const char *dead;  // extra lines of the profile (the dead zone of the lift)
};
static const MotionCase kMotionCases[] = {
    {"v_push", "V_PUSH_ACTION", 0, 0, 300, 0, 0, 0, ""},
    {"v_tilt", "V_TILT_ACTION", 0, 0, 0, 300, 0, 0, ""},
    {"h_push", "H_PUSH_ACTION", 300, 0, 0, 0, 0, 0, ""},
    {"h_tilt", "H_TILT_ACTION", 0, 0, 0, 0, 0, 300, ""},
    {"twist", "TWIST_ACTION", 0, 0, 0, 0, 300, 0, ""},
    {"lift", "LIFT_ACTION", 0, 300, 0, 0, 0, 0, "LIFT_DEADZONE = 20\n"},
};
static const char *const kFunctions[] = {"hold:click,click", "hold:ctrl+shift,alt", "hold:rightclick,ctrl", "step:click,rightclick"};
static const char *const kTransitions[] = {"reload", "ENABLED=0", "program unbinds", "suspend", "unplugged", "mode switch", "silent source"};

// The cap held at (x, y, z, rx, ry, rz) for `secs`: the event is repeated every 100 ms, like a device does.
static void holdAt(StuckRig &r, int x, int y, int z, int rx, int ry, int rz, double secs) {
    for (double t = 0; t < secs - 1e-9; t += 0.1) {
        r.src.pushMotion(0, x, y, z, rx, ry, rz);
        r.run(0.1);
    }
}

static void pushCase(StuckRig &r, const MotionCase &m, int sign) {
    r.src.pushMotion(0, sign * m.x, sign * m.y, sign * m.z, sign * m.rx, sign * m.ry, sign * m.rz);
}

// Runs `secs` while the cap stays deflected: the event is repeated every 100 ms, like a device does (a silent source is a case of
// its own, "silent source" below; the stale watchdog zeroes the axes after a quarter of a second without an event).
static void runDeflected(StuckRig &r, const MotionCase &m, int sign, double secs) {
    for (double t = 0; t < secs - 1e-9; t += 0.1) {
        pushCase(r, m, sign);
        r.run(0.1);
    }
}

static void testMouseHoldEveryTransition() {
    for (const MotionCase &m : kMotionCases) {
        for (const char *fn : kFunctions) {
            for (int sign = -1; sign <= 1; sign += 2) {
                for (int tr = 0; tr < 7; ++tr) {
                    char what[240];
                    std::snprintf(what, sizeof what, "%s %s (%s) / %s", m.name, fn, sign < 0 ? "negative" : "positive", kTransitions[tr]);
                    const std::string line = std::string(m.key) + " = " + fn + "\n" + m.dead;
                    const std::string unbind = std::string(m.key) + " = none\n";
                    const bool viaMode = tr == 5;
                    StuckRig r(viaMode ? std::string("[global]\nTICK_HZ = 250\nMODE = alpha\n")
                                       : "[global]\nTICK_HZ = 250\n" + line + "[app:off]\nENABLED = 0\n[app:unb]\n" + unbind,
                               viaMode ? std::vector<std::pair<std::string, std::string>>{{"alpha.conf", line + "BUTTON_2 = mode:next\n"},
                                                                                          {"beta.conf", unbind + "BUTTON_2 = mode:next\n"}}
                                       : std::vector<std::pair<std::string, std::string>>{});
                    r.run(0.1);
                    runDeflected(r, m, sign, 0.3);
                    const bool isHold = fn[0] == 'h';
                    // a side that is not the deflected one does nothing; both sides are filled in the cases, so something happens
                    CHECK(isHold ? r.rec.held() : r.rec.clicks > 0, "%s: the function acts while the cap is deflected", what);
                    switch (tr) {
                    case 0:
                        writeFile(r.conf, "[global]\nTICK_HZ = 250\n" + unbind);
                        r.reload.pending = true;
                        break;
                    case 1: r.win.cls = "off"; break;
                    case 2: r.win.cls = "unb"; break;
                    case 3: r.suspend = true; break;
                    case 4:
                        r.devicesChanged();
                        r.src.list.clear();
                        break;
                    case 5: r.src.pushButton(0, 1, true); break;
                    case 6: break;  // the source says nothing more
                    }
                    auto stillDeflected = [&](double secs) {
                        if (tr == 4 || tr == 6) r.run(secs);  // an unplugged device and a silent source send nothing more
                        else runDeflected(r, m, sign, secs);
                    };
                    stillDeflected(0.6);
                    CHECK(!r.rec.held(), "%s: still held after the transition (%zu keys, %zu mouse)", what, r.rec.keys.size(), r.rec.mouse.size());
                    const int before = r.rec.clicks;
                    stillDeflected(0.5);  // the cap is still deflected (or silent): the cause is gone, no click may follow, nothing pressed again
                    CHECK(r.rec.clicks == before && !r.rec.held(), "%s: output after the transition (%d clicks)", what, r.rec.clicks - before);
                    if (tr == 5) r.src.pushButton(0, 1, false);
                    r.stop();
                    if (tr == 3) r.suspend = false;
                    r.win.cls = "";
                    r.expectQuiet(what);
                }
            }
        }
    }
}

// A hold of ctrl on a tilt plus the twist that zooms (ctrl as the zoom modifier): every path ends with ctrl up.
static void testZoomPlusHoldEveryTransition() {
    for (int tr = 0; tr < 7; ++tr) {
        char what[160];
        std::snprintf(what, sizeof what, "zoom + hold ctrl / %s", kTransitions[tr]);
        const std::string line = "V_TILT_ACTION = hold:ctrl,ctrl\nTWIST_ACTION = zoom\n";
        const std::string unbind = "V_TILT_ACTION = none\nTWIST_ACTION = none\n";
        StuckRig r(tr == 5 ? std::string("[global]\nTICK_HZ = 250\nMODE = alpha\n")
                           : "[global]\nTICK_HZ = 250\n" + line + "[app:off]\nENABLED = 0\n[app:unb]\n" + unbind,
                   tr == 5 ? std::vector<std::pair<std::string, std::string>>{{"alpha.conf", line + "BUTTON_2 = mode:next\n"},
                                                                              {"beta.conf", unbind + "BUTTON_2 = mode:next\n"}}
                           : std::vector<std::pair<std::string, std::string>>{});
        r.run(0.1);
        holdAt(r, 0, 0, 0, 300, 350, 0, 0.4);  // tilt and twist
        CHECK(r.rec.keys.count(int(Key::Ctrl)), "%s: ctrl is down with zoom and hold", what);
        switch (tr) {
        case 0:
            writeFile(r.conf, "[global]\nTICK_HZ = 250\n" + unbind);
            r.reload.pending = true;
            break;
        case 1: r.win.cls = "off"; break;
        case 2: r.win.cls = "unb"; break;
        case 3: r.suspend = true; break;
        case 4:
            r.devicesChanged();
            r.src.list.clear();
            break;
        case 5: r.src.pushButton(0, 1, true); break;
        case 6: break;
        }
        r.run(0.6);
        CHECK(!r.rec.held(), "%s: ctrl still held after the transition", what);
        if (tr == 5) r.src.pushButton(0, 1, false);
        r.stop();
        if (tr == 3) r.suspend = false;
        r.win.cls = "";
        r.expectQuiet(what);
    }
    // both end by themselves: ctrl stays down until the later one lets go
    StuckRig r("[global]\nTICK_HZ = 250\nV_TILT_ACTION = hold:ctrl,ctrl\nTWIST_ACTION = zoom\n");
    r.run(0.1);
    holdAt(r, 0, 0, 0, 300, 350, 0, 0.4);
    holdAt(r, 0, 0, 0, 0, 350, 0, 0.4);  // the tilt lets go, the twist zooms on
    CHECK(r.rec.keys.count(int(Key::Ctrl)), "the zoom keeps ctrl after the hold ended");
    r.stop();
    r.expectQuiet("zoom + hold, both end");
}

// The safety net: the output holds something the runner does not know (a lost key-up) after a hold: it must be cleared.
static void testSafetyNetAfterHold() {
    StuckRig r("[global]\nTICK_HZ = 250\nV_TILT_ACTION = hold:click,click\n");
    r.run(0.1);
    holdAt(r, 0, 0, 0, 300, 0, 0, 0.2);
    CHECK(r.rec.mouse.size() == 1, "the hold presses the button");
    r.stop();
    r.run(0.1);
    CHECK(!r.rec.held(), "released with the motion");
    r.rec.mouse.insert(1);  // the output desynchronises: a button that the runner does not know
    r.rec.keys.insert(int(Key::Ctrl));
    const int before = r.rec.releaseAlls;
    r.expectQuiet("safety net after a hold", 0.5);
    CHECK(r.rec.releaseAlls > before, "the safety net calls Output::releaseAll");
}

// A legitimate long hold survives the safety net (the deflection is activity).
static void testLongHoldSurvivesSafetyNet() {
    StuckRig r("[global]\nTICK_HZ = 250\nV_TILT_ACTION = hold:click,click\n");
    r.run(0.1);
    holdAt(r, 0, 0, 0, 300, 0, 0, 3.0);
    CHECK(r.rec.mouse.size() == 1, "a drag with hold is not broken by the safety net");
    r.stop();
    r.expectQuiet("long hold");
}

// --- macros (stage 7b of docs/plan-motion-functions.md) ------------------------------------------------------------------
// A macro holds keys between its steps (here ctrl and a, for 5 s). Every way the cause can disappear that releases everything
// (reload, ENABLED = 0, suspend, an unplugged device, a joystick mode switch) must abort it at once and release what it holds; a
// finished macro leaves nothing held. A change of the program does not abort it (a macro that switches windows changes the
// program itself).
static const char *const kMacroLine = "BUTTON_1 = macro:+ctrl +a wait:5000 -a -ctrl\n";
static const char *const kMacroTransitions[] = {"reload", "ENABLED=0", "suspend", "unplugged", "mode switch"};

static void testMacroEveryTransition() {
    for (int tr = 0; tr < 5; ++tr) {
        char what[120];
        std::snprintf(what, sizeof what, "macro / %s", kMacroTransitions[tr]);
        StuckRig r(tr == 4 ? std::string("[global]\nTICK_HZ = 250\nMODE = alpha\n")
                           : std::string("[global]\nTICK_HZ = 250\n") + kMacroLine + "[app:off]\nENABLED = 0\n",
                   tr == 4 ? std::vector<std::pair<std::string, std::string>>{{"alpha.conf", std::string(kMacroLine) + "BUTTON_2 = mode:next\n"},
                                                                              {"beta.conf", "BUTTON_2 = mode:next\n"}}
                           : std::vector<std::pair<std::string, std::string>>{});
        r.run(0.1);
        r.src.pushButton(0, 0, true);
        r.run(0.1);
        r.src.pushButton(0, 0, false);
        r.run(0.1);
        CHECK(r.rec.keys.count(int(Key::Ctrl)) && r.rec.keys.count(int(Key::A)), "%s: the macro holds ctrl and a", what);
        CHECK(r.core->runner().macroRunning(), "%s: and runs", what);
        switch (tr) {
        case 0:
            writeFile(r.conf, "[global]\nTICK_HZ = 250\n");  // the macro is gone from the profile
            r.reload.pending = true;
            break;
        case 1: r.win.cls = "off"; break;
        case 2: r.suspend = true; break;
        case 3:
            r.devicesChanged();
            r.src.list.clear();
            break;
        case 4: r.src.pushButton(0, 1, true); break;
        }
        r.run(0.6);
        CHECK(!r.rec.held(), "%s: still held after the transition (%zu keys)", what, r.rec.keys.size());
        CHECK(tr == 3 || !r.core->runner().macroRunning(), "%s: the macro is aborted", what);
        const int before = r.rec.output();
        r.run(0.5);
        CHECK(!r.rec.held() && r.rec.output() == before, "%s: nothing follows", what);
        if (tr == 4) r.src.pushButton(0, 1, false);
        if (tr == 2) r.suspend = false;
        r.win.cls = "";
        r.expectQuiet(what);
    }
}

// The transition that is none: the source goes silent (the stale watchdog zeroes the axes, a quiet device sends no buttons either).
// The macro does not depend on the device, so it ends by itself and releases at its end.
static void testMacroSilentSource() {
    StuckRig r("[global]\nTICK_HZ = 250\nBUTTON_1 = macro:+ctrl +a wait:600 -a\n");
    r.run(0.1);
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.src.pushButton(0, 0, false);
    r.run(0.3);
    CHECK(r.rec.keys.count(int(Key::Ctrl)) && r.rec.keys.count(int(Key::A)), "the macro holds ctrl and a");
    r.run(0.7);  // the source says nothing more; the pause is over
    CHECK(!r.rec.held() && !r.core->runner().macroRunning(), "released at the end of the macro (%zu keys)", r.rec.keys.size());
    r.expectQuiet("macro, silent source");
}

// A pause longer than the quiet period of the safety net (250 ms) is no reason to cut the macro; the net still clears
// what the output holds without the runner knowing.
static void testMacroSafetyNet() {
    StuckRig r("[global]\nTICK_HZ = 250\nBUTTON_1 = macro:+a wait:900 -a\n");
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.src.pushButton(0, 0, false);
    r.run(0.6);  // well beyond the quiet period, the macro is waiting
    CHECK(r.rec.keys.count(int(Key::A)) && r.core->runner().macroRunning(), "the net does not cut a macro that waits");
    r.run(0.25);  // the macro ends at 0.9 s, the net would act 250 ms later
    CHECK(!r.rec.held() && !r.core->runner().macroRunning(), "the macro ended and released a");
    r.rec.keys.insert(int(Key::Ctrl));  // the output desynchronises
    const int before = r.rec.releaseAlls;
    r.expectQuiet("safety net after a macro", 0.5);
    CHECK(r.rec.releaseAlls > before, "the safety net calls Output::releaseAll after the macro");
}

// The program changes while the macro runs (for example alt+tab): the macro goes on and ends by itself.
static void testMacroSurvivesProgramChange() {
    StuckRig r("[global]\nTICK_HZ = 250\nBUTTON_1 = macro:+alt +tab wait:400 -tab -alt\n[app:other]\nZOOM_SENS = 2\n");
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.src.pushButton(0, 0, false);
    r.run(0.05);
    CHECK(r.rec.keys.count(int(Key::Alt)), "alt is down");
    r.win.cls = "other";  // the window switch that alt+tab causes
    r.run(0.15);
    CHECK(r.rec.keys.count(int(Key::Alt)) && r.rec.keys.count(int(Key::Tab)) && r.core->runner().macroRunning(), "the macro goes on");
    r.run(0.6);
    CHECK(!r.rec.held() && !r.core->runner().macroRunning(), "and releases at its end");
    r.win.cls = "";
    r.expectQuiet("macro, program change");
}

// A macro on a motion: pushing triggers it once, an aborted run leaves nothing held and the motion that stays deflected starts nothing.
static void testMotionMacroTransitions() {
    for (int tr = 0; tr < 3; ++tr) {
        char what[120];
        std::snprintf(what, sizeof what, "motion macro / %s", tr == 0 ? "reload" : tr == 1 ? "ENABLED=0" : "suspend");
        const std::string line = "V_PUSH_ACTION = macro:+ctrl +a wait:5000 -a -ctrl,+ctrl +a wait:5000 -a -ctrl\n";
        StuckRig r("[global]\nTICK_HZ = 250\n" + line + "[app:off]\nENABLED = 0\n");
        r.run(0.1);
        for (int i = 0; i < 3; ++i) {
            r.src.pushMotion(0, 0, 0, 300, 0, 0, 0);
            r.run(0.1);
        }
        CHECK(r.rec.keys.count(int(Key::Ctrl)) && r.rec.keys.count(int(Key::A)), "%s: the macro holds ctrl and a", what);
        switch (tr) {
        case 0:
            writeFile(r.conf, "[global]\nTICK_HZ = 250\n");
            r.reload.pending = true;
            break;
        case 1: r.win.cls = "off"; break;
        case 2: r.suspend = true; break;
        }
        for (int i = 0; i < 6; ++i) {  // the cap stays deflected
            r.src.pushMotion(0, 0, 0, 300, 0, 0, 0);
            r.run(0.1);
        }
        CHECK(!r.rec.held() && !r.core->runner().macroRunning(), "%s: nothing held, no macro", what);
        r.stop();
        if (tr == 2) r.suspend = false;
        r.win.cls = "";
        r.expectQuiet(what);
    }
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
    testSplitPartZoomHoldsCtrl();
    testSplitPartSilentSource();
    testSplitPartDeviceUnplugged();
    testSplitPartModeSwitchMidMotion();
    testSplitPartReloadMidMotion();
    testEveryActionEveryTransition();
    testZoomModifiersEveryTransition();
    testNavLatchCleared();
    testKeyEngineEveryTransition();
    testKeyEngineLift();
    testMouseHoldEveryTransition();
    testZoomPlusHoldEveryTransition();
    testSafetyNetAfterHold();
    testLongHoldSurvivesSafetyNet();
    testMacroEveryTransition();
    testMacroSilentSource();
    testMacroSafetyNet();
    testMacroSurvivesProgramChange();
    testMotionMacroTransitions();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_stuck_keys: all passed");
    return 0;
}
