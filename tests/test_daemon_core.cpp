// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Drives the platform-independent daemon core with a scripted input source, a recording
// output, a fake active-window provider and a fake reload channel (no OS, no threads).
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "daemon_core.hpp"
#include "builtin_counts.hpp"
#include "mode_request.hpp"
#include "modes.hpp"
#include "platform/fake_input.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct DaemonRec {
    int wv = 0, wh = 0, abs_zoom = 0, ctrl_presses = 0;
    bool ctrl = false;
    std::vector<std::string> mouse;
};

struct RecOutput : Output {
    DaemonRec *r;
    explicit RecOutput(DaemonRec *rec) : r(rec) {}
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override {
        r->wv += n;
        if (r->ctrl) r->abs_zoom += n < 0 ? -n : n;
    }
    void wheelH(int n) override { r->wh += n; }
    void setKey(Key k, bool d) override {
        if (k == Key::Ctrl) {
            if (d && !r->ctrl) ++r->ctrl_presses;
            r->ctrl = d;
        }
    }
    void tapKey(Key) override {}
    void mouseButton(int b, bool d) override { r->mouse.push_back((d ? "+" : "-") + std::to_string(b)); }
    void releaseAll() override {}
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
    bool pollReload() override {
        bool p = pending;
        pending = false;
        return p;
    }
};

// FakeInputSource with a changeable device list.
struct TestSource : FakeInputSource {
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> devices() const override { return list; }
    InputMode mode = InputMode::Auto;
    int modeCalls = 0;
    std::string label;  // what pathLabel() reports (the hybrid input of Windows and macOS)
    void setInputMode(InputMode m) override {
        mode = m;
        ++modeCalls;
    }
    std::string pathLabel() const override { return label; }
};

static void writeFile(const std::string &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    f << text;
}

struct Rig {
    std::string dir, conf;
    DaemonRec rec;
    TestSource src;
    FakeWindow win;
    FakeReload reload;
    int64_t now = 10'000'000'000;  // fake clock (ns), well above the stale window
    std::unique_ptr<DaemonCore> core;

    std::vector<std::string> status;
    std::vector<LiveSample> live;  // what the live feed sink received, one entry per tick
    explicit Rig(const std::string &config, const std::vector<std::pair<std::string, std::string>> &modeFiles = {})
        : dir(tu::makeTempDir("dofscroll-core-test")) {
        conf = dir + "/dofscroll.conf";
        writeFile(conf, config);
        if (!modeFiles.empty()) {
            assert(std::filesystem::create_directory(dir + "/modes"));
            for (const auto &f : modeFiles) writeFile(dir + "/modes/" + f.first, f.second);
        }
        DeviceInfo di;
        di.name = "Test Mouse A";
        di.reportedButtons = 2;
        src.list = {di};
        DaemonDeps deps;
        deps.input = &src;
        deps.activeWindow = &win;
        deps.reload = &reload;
        deps.paths.configDir = dir;  // config: <dir>/dofscroll.conf, modes: <dir>/modes (absent)
        deps.paths.runtimeDir = dir;
        DaemonRec *rp = &rec;
        deps.makeOutput = [rp](const std::string &, std::string *) {
            return std::unique_ptr<Output>(new RecOutput(rp));
        };
        deps.clock = [this] { return now; };
        deps.writeStatus = [this](const std::string &body, int64_t) { status.push_back(body); };
        deps.liveFeed = [this](const LiveSample &s, int64_t) { live.push_back(s); };
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~Rig() {
        tu::removeAll(dir);
    }

    void writeButtonMode(const std::string &file, const std::string &text) {
        std::filesystem::create_directory(dir + "/buttons");
        writeFile(dir + "/buttons/" + file, text);
    }

    void pump() {
        InputEvent e;
        while (src.wait(&e, 0) == WaitResult::Event) core->reader().handle(e);
    }
    // 250 Hz ticks, inputs delivered before each tick.
    void run(double secs) {
        const int n = static_cast<int>(secs * 250);
        for (int i = 0; i < n; ++i) {
            now += 4'000'000;
            pump();
            core->updateProfile(now);
            core->tickOnce(now, 0.004);
        }
    }
};

static const char *const BASE_CONF = "[global]\nTICK_HZ = 250\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 0\n";

static void testMotion() {
    Rig r(BASE_CONF);
    r.run(0.1);
    CHECK(r.rec.wv == 0 && r.rec.wh == 0, "idle must not scroll");
    // Same peak vectors as tests/test_scenarios.cpp ("Push v/z" and "Twist").
    r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    r.run(1.0);
    CHECK(r.rec.wv != 0, "no vertical scroll");
    CHECK(r.rec.abs_zoom == 0 && r.rec.ctrl_presses == 0, "zoom while scrolling");
    r.src.pushMotion(0, 0, 0, 0, 0, 0, 0);
    r.run(0.3);
    r.rec = DaemonRec();
    r.src.pushMotion(0, 82, 144, 90, 35, 350, 111);
    r.run(1.0);
    CHECK(r.rec.abs_zoom > 0 && r.rec.ctrl_presses >= 1, "twist must zoom (zoom=%d)", r.rec.abs_zoom);
    r.src.pushMotion(0, 0, 0, 0, 0, 0, 0);
    r.run(0.5);
    CHECK(!r.rec.ctrl, "Ctrl must be released after the twist");
}

static void testStaleWatchdog() {
    Rig r(BASE_CONF);
    r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    r.run(0.5);
    CHECK(r.rec.wv != 0, "scrolling expected");
    // No further events: after 250 ms the state counts as stale and output stops.
    r.run(0.4);
    int before = r.rec.wv;
    r.run(0.5);
    CHECK(r.rec.wv == before, "stale deflection must not scroll on");
}

static void testLiveFeedSink() {
    Rig r(BASE_CONF);
    r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    r.run(0.15);  // inside the 250 ms stale window
    CHECK(r.live.size() >= 30, "sink must be called once per tick");
    const LiveSample &s = r.live.back();
    CHECK(s.active && s.enabled && !s.idle && s.fresh, "live flags");
    CHECK(s.axes[AX_X] == 69 && s.axes[AX_Y] == 286 && s.axes[AX_Z] == 350, "raw axes in the sample");
    CHECK(s.v != 0.0f && s.mode == "normal", "channel output and mode in the sample");
    CHECK(s.devices.size() == 1 && s.devices[0].name == "Test Mouse A" && s.devices[0].factoryButtons == 2 &&
              s.devicesRev >= 1,
          "device info in the sample");
    CHECK(s.source == s.devices[0].id, "the sample names the device it comes from (issue #139)");
    Rig off("[global]\nLIVE_FEED = 0\n");
    off.run(0.1);
    CHECK(!off.live.empty() && !off.live.back().active, "LIVE_FEED = 0 marks the sample inactive");
    // A profile with ENABLED = 0 stops the output but the feed still shows the device.
    Rig dis("[global]\nENABLED = 0\n");
    dis.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    dis.run(0.1);
    CHECK(dis.live.back().idle && !dis.live.back().enabled && dis.live.back().axes[AX_Z] == 350,
          "feed shows raw axes while idle");
}

static void testButtonBinding() {
    Rig r("[global]\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 1\n");
    r.run(0.1);
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    r.src.pushButton(0, 1, false);
    r.run(0.1);
    CHECK((r.rec.mouse == std::vector<std::string>{"+1", "-1"}), "click binding: got %zu events",
          r.rec.mouse.size());
    // A different button does nothing.
    r.rec.mouse.clear();
    r.src.pushButton(0, 0, true);
    r.run(0.1);
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    CHECK(r.rec.mouse.empty(), "unbound button must not click");
}

static void testReload() {
    Rig r(BASE_CONF);
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    r.src.pushButton(0, 1, false);
    r.run(0.1);
    CHECK(r.rec.mouse.empty(), "click disabled before reload");
    CHECK(!r.core->settings().click_button_enabled, "click disabled in settings");

    writeFile(r.conf, "[global]\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 1\n");
    r.reload.pending = true;  // request, no waiting for the 1 s mtime poll
    r.run(0.02);
    CHECK(r.core->settings().click_button_enabled, "reload must re-read the config");
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    r.src.pushButton(0, 1, false);
    r.run(0.1);
    CHECK((r.rec.mouse == std::vector<std::string>{"+1", "-1"}), "click after reload: %zu events",
          r.rec.mouse.size());
}

// INPUT reaches the input source at start and on reload; the active path appears in the status file.
static void testInputMode() {
    Rig r("[global]\nINPUT = driver\n");
    CHECK(r.src.mode == InputMode::Driver && r.src.modeCalls >= 1, "INPUT=driver must reach the source");
    r.run(0.05);
    CHECK(!r.status.empty() && r.status.back().find("input=") == std::string::npos, "no input= line without a path label");
    r.src.label = "driver";
    r.run(0.05);
    CHECK(r.status.back().find("\ninput=driver\n") != std::string::npos, "status must name the driver path");
    r.src.label = "hid";
    writeFile(r.conf, "[global]\nINPUT = hid\n");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.src.mode == InputMode::Hid, "reload must apply INPUT=hid");
    CHECK(r.status.back().find("\ninput=hid\n") != std::string::npos, "status must name the HID path");
    writeFile(r.conf, "[global]\nINPUT = bogus\n");  // invalid: the line is ignored, the default applies
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.src.mode == InputMode::Auto, "an invalid INPUT falls back to auto");
    writeFile(r.conf, "[global]\nINPUT = hid\n[app:blender]\nINPUT = driver\n");  // process-level: only [global] counts
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.src.mode == InputMode::Hid, "INPUT in a profile does not change the process-level setting");
}

static void testAutoReload() {
    Rig r(BASE_CONF);
    writeFile(r.conf, "[global]\nTICK_HZ = 120\nCLICK_BUTTON_ENABLED = 1\n");
    r.run(1.2);  // the file stamp is polled about once a second
    CHECK(r.core->settings().tick_hz == 120, "changed file must be picked up (tick_hz=%d)",
          r.core->settings().tick_hz);
}

static void testDeviceAndWindowProfiles() {
    Rig r("[global]\nTICK_HZ = 250\n[device:Test Mouse B]\nTICK_HZ = 100\n[app:firefox]\nTICK_HZ = 60\n");
    r.run(0.1);
    CHECK(r.core->deviceName() == "Test Mouse A", "initial device '%s'", r.core->deviceName().c_str());
    CHECK(r.core->settings().tick_hz == 250, "global profile");

    DeviceInfo b;
    b.name = "Test Mouse B";
    b.reportedButtons = 2;
    r.src.list = {b};
    InputEvent ev;
    ev.type = InputEventType::DevicesChanged;
    r.src.push(ev);
    r.run(0.1);
    CHECK(r.core->deviceName() == "Test Mouse B", "device change: '%s'", r.core->deviceName().c_str());
    CHECK(r.core->settings().tick_hz == 100, "[device:*] profile applies (%d)", r.core->settings().tick_hz);

    r.win.cls = "navigator firefox";
    r.run(0.3);
    CHECK(r.core->settings().tick_hz == 60, "[app:*] profile applies (%d)", r.core->settings().tick_hz);
    CHECK(r.core->matchDesc().find("app:firefox") != std::string::npos, "match desc '%s'",
          r.core->matchDesc().c_str());
}

// A program profile has the last word over the joystick mode, also when the collapsed values (allKeys) of the
// profile before and after the switch are equal: the program layer (app_buttons, actions[1]) is part of the comparison.
static void testProgramLayerSwitch() {
    {   // button: the mode binds button 1 to middleclick, [device] and [app] both say click
        Rig r(std::string(BASE_CONF) + "MODE = mymode\n[device:Test Mouse A]\nBUTTON_1 = click\n[app:ed]\nBUTTON_1 = click\n",
              {{"mymode.conf", "BUTTON_1 = middleclick\n"}});
        r.run(0.1);
        r.src.pushButton(0, 0, true);
        r.run(0.1);
        r.src.pushButton(0, 0, false);
        r.run(0.1);
        CHECK((r.rec.mouse == std::vector<std::string>{"+2", "-2"}), "mode binding wins over the device profile (%zu events)",
              r.rec.mouse.size());
        r.rec = DaemonRec();
        r.win.cls = "ed";
        r.run(0.3);
        r.src.pushButton(0, 0, true);
        r.run(0.1);
        r.src.pushButton(0, 0, false);
        r.run(0.1);
        CHECK((r.rec.mouse == std::vector<std::string>{"+1", "-1"}), "the program's click wins after the switch (%zu events)",
              r.rec.mouse.size());
    }
    {   // channel action: device zoom, mode wheel_h, program zoom
        Rig r(std::string(BASE_CONF) + "MODE = mymode\n[device:Test Mouse A]\nV_ACTION = zoom\n[app:ed]\nV_ACTION = zoom\n",
              {{"mymode.conf", "V_ACTION = wheel_h\n"}});
        r.run(0.1);
        r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
        r.run(1.0);
        CHECK(r.rec.ctrl_presses == 0 && r.rec.wh != 0, "mode action wins over the device profile (wh=%d)", r.rec.wh);
        r.src.pushMotion(0, 0, 0, 0, 0, 0, 0);
        r.run(0.3);
        r.rec = DaemonRec();
        r.win.cls = "ed";
        r.run(0.3);
        r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
        r.run(1.0);
        CHECK(r.rec.ctrl_presses >= 1 && r.rec.abs_zoom > 0, "the program's zoom wins after the switch (zoom=%d)", r.rec.abs_zoom);
    }
}

// The same through an edit of the file (no forced mode reload): the matching sections (and so the profile description) stay the same, the collapsed
// values stay equal, only the program layer changes.
static void testProgramLayerReload() {
    auto click = [](Rig &r) {
        r.rec = DaemonRec();
        r.src.pushButton(0, 0, true);
        r.run(0.1);
        r.src.pushButton(0, 0, false);
        r.run(0.1);
        return r.rec.mouse;
    };
    const std::string head = std::string(BASE_CONF) + "MODE = mymode\n[device:Test Mouse A]\nBUTTON_1 = click\nV_ACTION = zoom\n";
    Rig r(head + "[app:ed]\nTICK_HZ = 250\n", {{"mymode.conf", "BUTTON_1 = middleclick\nV_ACTION = wheel_h\n"}});
    r.win.cls = "ed";
    r.run(0.3);
    CHECK((click(r) == std::vector<std::string>{"+2", "-2"}), "mode binding wins over the device profile");
    r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    r.run(1.0);
    CHECK(r.rec.ctrl_presses == 0 && r.rec.wh != 0, "mode action wins over the device profile");
    r.src.pushMotion(0, 0, 0, 0, 0, 0, 0);
    r.run(0.3);
    writeFile(r.conf, head + "[app:ed]\nTICK_HZ = 250\nBUTTON_1 = click\nV_ACTION = zoom\n");
    r.run(3.0);  // the file stamp is polled about once a second: an edit of the file, not a reload request
    CHECK((click(r) == std::vector<std::string>{"+1", "-1"}), "the program's click wins after the edit");
    r.rec = DaemonRec();
    r.src.pushMotion(0, 69, 286, 350, 145, 56, 155);
    r.run(1.0);
    CHECK(r.rec.ctrl_presses >= 1 && r.rec.abs_zoom > 0, "the program's zoom wins after the edit (zoom=%d)", r.rec.abs_zoom);
}

static void testDeviceRemovedReleasesState() {
    Rig r("[global]\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 1\n");
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    CHECK(r.rec.mouse.size() == 1 && r.rec.mouse[0] == "+1", "button held");
    // Unplug: no release event ever arrives, the core must not leave the click held.
    InputEvent ev;
    ev.type = InputEventType::DevicesChanged;
    r.src.push(ev);
    r.run(0.1);
    CHECK(r.rec.mouse.size() == 2 && r.rec.mouse[1] == "-1", "click released on device removal");
}

static std::string statusMode(const Rig &r) {
    if (r.status.empty()) return "";
    const std::string &b = r.status.back();
    const size_t p = b.find("mode=");
    return p == std::string::npos ? "" : b.substr(p + 5, b.find('\n', p) - p - 5);
}

// The ids mode:next / mode:prev cycle through: the built-ins (whatever the embedded library holds)
// plus the given user ids, sorted. The tests below use the user ids "0-first" (sorts before every
// built-in id, which start with a letter) and "zzzz-last" (after every one), so their positions do not
// depend on how many built-ins exist or what they are called.
static std::vector<std::string> cycleIds(const std::vector<std::string> &user) {
    std::vector<std::string> ids = tu::sortedIds(spn::loadModes("", nullptr));
    for (const std::string &u : user)
        if (std::find(ids.begin(), ids.end(), u) == ids.end()) ids.push_back(u);
    std::sort(ids.begin(), ids.end());
    return ids;
}
// The id after `id` in the cycle, wrapping.
static std::string cycleNext(const std::vector<std::string> &ids, const std::string &id) {
    const size_t i = std::find(ids.begin(), ids.end(), id) - ids.begin();
    return ids[(i + 1) % ids.size()];
}
static void testModeSwitchButton() {
    // ids: 0-first < <the built-ins in alphabetical order> < zzzz-last
    const std::vector<std::string> ids = cycleIds({"0-first", "zzzz-last"});
    const std::string firstBuiltin = ids[1];
    assert(ids.front() == "0-first" && ids.back() == "zzzz-last" && ids.size() == tu::builtinModeCount() + 2);
    Rig r(std::string(BASE_CONF) + "MODE = 0-first\n",
          {{"0-first.conf", "BUTTON_2 = mode:next\nBUTTON_3 = key:space\n"},
           {"zzzz-last.conf", "BUTTON_2 = mode:next\nBUTTON_4 = mode:off\n"}});
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "0-first", "starts in the configured mode");
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    // hold a key via BUTTON_3, then switch with BUTTON_2 while it is held
    r.src.pushButton(0, 2, true);
    r.run(0.1);
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    CHECK(r.core->runner().mode().id == firstBuiltin, "next after 0-first is the first built-in id %s (got %s)",
          firstBuiltin.c_str(), r.core->runner().mode().id.c_str());
    r.src.pushButton(0, 1, false);
    r.src.pushButton(0, 2, false);
    r.run(0.1);
    CHECK(statusMode(r) == firstBuiltin, "status shows the effective mode (%s)", statusMode(r).c_str());
    // unrelated profile re-resolution (app change) keeps the override
    r.win.cls = "navigator firefox";
    r.run(0.3);
    CHECK(r.core->runner().mode().id == firstBuiltin, "override is sticky across profile changes");
    // the built-in has no binding: the user cannot cycle further from there. Reload clears the override.
    writeFile(r.dir + "/modes/0-first.conf", "BUTTON_2 = mode:next\n");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "0-first", "reload clears the override (%s)", r.core->runner().mode().id.c_str());
    // cycle through all modes and wrap: 0-first -> first built-in; use zzzz-last to wrap by going to last
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = zzzz-last\n");
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "zzzz-last", "zzzz-last active");
    press(1);  // next wraps around from the last id
    CHECK(r.core->runner().mode().id == "0-first", "next wraps to the first id");
    press(1);  // next -> normal
    CHECK(r.core->runner().mode().id == firstBuiltin, "0-first -> first built-in");
    CHECK(!r.rec.ctrl, "no stray state");
    r.reload.pending = true;  // back to zzzz-last
    r.run(0.05);
    r.src.pushButton(0, 3, true);
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "off", "fixed target mode:off");
}

// The built-in library takes part in the cycle: a user file with the id of a built-in can start it,
// the next id in alphabetical order is the next built-in, and prev wraps to the last id. The
// built-ins used are the first two and the last one of the embedded library, whatever they are.
static void testModeCycleBuiltins() {
    const std::vector<std::string> ids = cycleIds({"zzzz-last"});
    assert(ids.size() >= 4 && ids.back() == "zzzz-last");
    const std::string b0 = ids[0], b1 = ids[1], bLast = ids[ids.size() - 2];
    const std::string bind = "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n";
    Rig r(std::string(BASE_CONF) + "MODE = " + b0 + "\n",
          {{b0 + ".conf", bind}, {b1 + ".conf", bind}, {"zzzz-last.conf", "BUTTON_1 = mode:prev\n"}});
    r.run(0.1);
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    auto id = [&] { return r.core->runner().mode().id; };
    CHECK(id() == b0, "starts in the overridden built-in %s (%s)", b0.c_str(), id().c_str());
    press(1);
    CHECK(id() == b1, "next after %s is the built-in %s (%s)", b0.c_str(), b1.c_str(), id().c_str());
    press(0);
    CHECK(id() == b0, "prev from %s is %s (%s)", b1.c_str(), b0.c_str(), id().c_str());
    press(0);
    CHECK(id() == "zzzz-last", "prev from the first id wraps to the last (%s)", id().c_str());
    press(0);
    CHECK(id() == bLast, "prev from zzzz-last is the last built-in %s (%s)", bLast.c_str(), id().c_str());
}

// mode:prev is the mirror of mode:next: alphabetical ids, wrapping in both directions.
static void testModePrev() {
    // ids: 0-first < <the built-ins> < zzzz-last
    const std::vector<std::string> ids = cycleIds({"0-first", "zzzz-last"});
    const std::string firstBuiltin = ids[1], lastBuiltin = ids[ids.size() - 2];
    Rig r(std::string(BASE_CONF) + "MODE = 0-first\n",
          {{"0-first.conf", "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n"},
           {"zzzz-last.conf", "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n"}});
    r.run(0.1);
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    auto id = [&] { return r.core->runner().mode().id; };
    CHECK(id() == "0-first", "starts in 0-first");
    press(0);  // prev from the first id wraps to the last
    CHECK(id() == "zzzz-last", "prev wraps from the first to the last (%s)", id().c_str());
    press(0);
    CHECK(id() == lastBuiltin, "prev goes to the previous id (%s)", id().c_str());
    // the built-in has no bindings of its own: continue from a mode that has them (reload, start at zzzz-last)
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = zzzz-last\n");
    r.run(0.05);
    CHECK(id() == "zzzz-last", "zzzz-last active after the reload");
    press(1);  // next from the last id wraps to the first
    CHECK(id() == "0-first", "next wraps from the last to the first (%s)", id().c_str());
    press(1);  // next -> the first built-in
    CHECK(id() == firstBuiltin, "0-first -> first built-in");
    // next followed by prev returns to the start
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = 0-first\n");
    r.run(0.05);
    press(1);
    CHECK(id() == firstBuiltin, "next from 0-first");
    // (the built-in has no binding: go back through the file mode instead)
    r.reload.pending = true;
    r.run(0.05);
    press(0);
    press(1);
    CHECK(id() == "0-first", "prev then next returns to the start (%s)", id().c_str());
}

// A button mode with CYCLE: next/prev walk exactly that list in that order, wrap around, start at
// the first/last entry when the effective mode is not in the list, skip unknown ids; a button mode
// without CYCLE keeps the alphabetical cycle over all modes.
static void testModeCycleList() {
    Rig r(std::string(BASE_CONF) + "MODE = normal\nBUTTON_1 = buttonmode:fwd\nBUTTON_2 = buttonmode:back\n"
                                   "BUTTON_3 = buttonmode:all\n",
          {{"alpha.conf", "SCROLL_V_SENS = 1\n"}, {"beta.conf", "SCROLL_V_SENS = 2\n"}, {"zed.conf", "SCROLL_V_SENS = 3\n"}});
    r.writeButtonMode("fwd.conf", "ACTION = mode:next\nCYCLE = zed, ghost, alpha ,zed, beta\n");
    r.writeButtonMode("back.conf", "ACTION = mode:prev\nCYCLE = zed, ghost, alpha, beta\n");
    r.writeButtonMode("all.conf", "ACTION = mode:next\n");
    r.reload.pending = true;
    r.run(0.1);
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    auto id = [&] { return r.core->runner().mode().id; };
    CHECK(id() == "normal", "starts in normal");
    press(0);  // not in the list: next goes to the first entry
    CHECK(id() == "zed", "next outside the list -> first entry (%s)", id().c_str());
    press(0);  // ghost is skipped
    CHECK(id() == "alpha", "next follows the list order, unknown id skipped (%s)", id().c_str());
    press(0);
    CHECK(id() == "beta", "next -> beta (%s)", id().c_str());
    press(0);
    CHECK(id() == "zed", "next wraps to the first entry (%s)", id().c_str());
    press(1);  // prev from the first wraps to the last
    CHECK(id() == "beta", "prev wraps to the last entry (%s)", id().c_str());
    press(1);
    CHECK(id() == "alpha", "prev goes to the previous entry (%s)", id().c_str());
    // reload: back to normal (not in the list); prev goes to the last entry
    r.reload.pending = true;
    r.run(0.1);
    CHECK(id() == "normal", "normal after the reload");
    press(1);
    CHECK(id() == "beta", "prev outside the list -> last entry (%s)", id().c_str());
    // without CYCLE: alphabetical over all loaded modes (built-ins and alpha, beta, zed)
    r.reload.pending = true;
    r.run(0.1);
    press(2);
    const std::string afterNormal = cycleNext(cycleIds({"alpha", "beta", "zed"}), "normal");
    CHECK(id() == afterNormal, "no CYCLE: alphabetical next after normal is %s (%s)", afterNormal.c_str(), id().c_str());
}

static std::string statusKey(const Rig &r, const std::string &key);

// hold:mode:<id>: effective while the button is held, then the state from before comes back.
static void testHoldMode() {
    // ids: alpha < fine < normal < off < timeline
    Rig r(std::string(BASE_CONF) + "BUTTON_3 = hold:mode:fine\nBUTTON_4 = mode:alpha\n",
          {{"fine.conf", "SCROLL_V_SENS = 1\n"}, {"alpha.conf", "BUTTON_1 = mode:prev\n"}});
    r.run(0.1);
    auto id = [&] { return r.core->runner().mode().id; };
    auto down = [&](int b) { r.src.pushButton(0, b, true); r.run(0.1); };
    auto up = [&](int b) { r.src.pushButton(0, b, false); r.run(0.1); };
    CHECK(id() == "normal", "starts in normal (%s)", id().c_str());

    // press: the mode is effective while held; fine has no binding for the button, release still works
    down(2);
    CHECK(id() == "fine", "held: fine (%s)", id().c_str());
    CHECK(statusKey(r, "toggle").find("hold:fine") != std::string::npos, "status shows the hold (%s)",
          statusKey(r, "toggle").c_str());
    up(2);
    CHECK(id() == "normal", "released: back to the profile's mode (%s)", id().c_str());
    CHECK(statusKey(r, "toggle").find("hold:") == std::string::npos, "status without hold");

    // a mode:<id> override that was in effect comes back
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "BUTTON_3 = hold:mode:fine\nBUTTON_4 = mode:alpha\n");
    r.run(0.05);
    down(3);
    up(3);
    CHECK(id() == "alpha", "mode:alpha override (%s)", id().c_str());
    // alpha binds button 1 only: the hold button 3 still comes from the main config
    down(2);
    CHECK(id() == "fine", "hold from the override state (%s)", id().c_str());
    up(2);
    CHECK(id() == "alpha", "the override comes back (%s)", id().c_str());

    // another switch during the hold ends it without restoring; the later release does nothing
    r.reload.pending = true;
    r.run(0.05);
    down(2);
    CHECK(id() == "fine", "held again");
    down(3);  // mode:alpha from the main config while holding
    up(3);
    CHECK(id() == "alpha", "another switch wins (%s)", id().c_str());
    up(2);
    CHECK(id() == "alpha", "the release of the hold button does nothing (%s)", id().c_str());

    // a reload clears the hold
    r.reload.pending = true;
    r.run(0.05);
    down(2);
    CHECK(id() == "fine", "held before the reload");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(id() == "normal", "reload drops the hold (%s)", id().c_str());
    up(2);
    CHECK(id() == "normal", "release after the reload does nothing");

    // suspend while holding: the mode must not stay in effect
    r.reload.pending = true;
    r.run(0.05);
    down(2);
    CHECK(id() == "fine", "held before the suspend");
    r.win.cls = "";
    r.core->updateProfile(r.now);
    InputEvent ev;
    ev.type = InputEventType::DevicesChanged;
    r.src.push(ev);  // unplugged: the buttons are gone, no release arrives
    r.run(0.2);
    CHECK(id() == "normal", "unplugging the device ends the hold (%s)", id().c_str());
}

// A user mode with the id "release" is an ordinary mode: mode:release switches to it, and the internal
// release of a hold:mode: button is not confused with it.
static void testModeNamedRelease() {
    Rig r(std::string(BASE_CONF) + "BUTTON_3 = hold:mode:fine\nBUTTON_4 = mode:release\n",
          {{"fine.conf", "SCROLL_V_SENS = 1\n"}, {"release.conf", "NAME = Release\n"}});
    r.run(0.1);
    auto id = [&] { return r.core->runner().mode().id; };
    auto down = [&](int b) { r.src.pushButton(0, b, true); r.run(0.1); };
    auto up = [&](int b) { r.src.pushButton(0, b, false); r.run(0.1); };
    down(3);
    CHECK(id() == "release", "mode:release switches to the mode file release.conf (%s)", id().c_str());
    up(3);
    CHECK(id() == "release", "releasing a non-hold button changes nothing (%s)", id().c_str());
    // the hold still ends with its own release request
    down(2);
    CHECK(id() == "fine", "hold:mode:fine while in the release mode (%s)", id().c_str());
    up(2);
    CHECK(id() == "release", "the hold ends and the previous mode comes back (%s)", id().c_str());
}

static std::string statusKey(const Rig &r, const std::string &key) {
    if (r.status.empty()) return "";
    const std::string &b = r.status.back();
    const size_t p = b.find("\n" + key + "=");
    if (p == std::string::npos) return "";
    const size_t from = p + key.size() + 2;
    return b.substr(from, b.find('\n', from) - from);
}

// toggle:mode:<id> and toggle:nav from the main config (they work in every mode): the first press
// switches, the second press goes back; a reload (also "Release all keys") clears both.
static void testToggleButtons() {
    Rig r(std::string(BASE_CONF) + "BUTTON_2 = toggle:mode:timeline\nBUTTON_3 = toggle:nav\n");
    r.run(0.1);
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    CHECK(r.core->runner().mode().id == "normal" && statusKey(r, "toggle").empty(), "starts plain");
    press(1);
    CHECK(r.core->runner().mode().id == "timeline", "first press switches (got %s)", r.core->runner().mode().id.c_str());
    CHECK(statusKey(r, "toggle") == "mode:timeline", "status shows the toggle (%s)", statusKey(r, "toggle").c_str());
    press(1);
    CHECK(r.core->runner().mode().id == "normal", "second press goes back (got %s)", r.core->runner().mode().id.c_str());
    CHECK(statusKey(r, "toggle").empty(), "status is empty again");

    press(2);
    CHECK(r.core->runner().navLatched() && statusKey(r, "toggle") == "nav", "nav latched (%s)", statusKey(r, "toggle").c_str());
    press(1);  // a mode switch clears the latch (it releases everything held)
    CHECK(!r.core->runner().navLatched() && statusKey(r, "toggle") == "mode:timeline", "switch clears nav (%s)",
          statusKey(r, "toggle").c_str());
    press(2);
    CHECK(statusKey(r, "toggle") == "nav mode:timeline", "both are listed (%s)", statusKey(r, "toggle").c_str());
    // the safety net (no input for a while) must not clear the latch
    r.run(3.0);
    CHECK(r.core->runner().navLatched(), "latch survives the quiet period");
    // a reload (what the tray's "Release all keys" requests) clears everything
    r.reload.pending = true;
    r.run(0.1);
    CHECK(!r.core->runner().navLatched() && r.core->runner().mode().id == "normal", "reload clears the toggles");
    CHECK(statusKey(r, "toggle").empty(), "status is empty after the reload (%s)", statusKey(r, "toggle").c_str());
}

// Runtime mode request from the tray: file in the runtime directory + reload, [global] untouched.
static void testModeRequest() {
    Rig r(std::string(BASE_CONF), {{"alpha.conf", "BUTTON_3 = key:space\n"}});
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "normal", "starts in the default mode");
    std::string err;
    CHECK(writeModeRequest(r.dir, "alpha", &err), "write request: %s", err.c_str());
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "normal", "no switch without a reload request");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "alpha", "request applied (%s)", r.core->runner().mode().id.c_str());
    CHECK(!tu::exists(modeRequestPath(r.dir)), "request file is consumed");
    CHECK(statusMode(r) == "alpha", "status shows the requested mode");
    // a request for an unknown mode keeps the default; the next plain reload drops the override
    CHECK(writeModeRequest(r.dir, "nope", &err), "write");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "normal", "unknown mode: default (%s)", r.core->runner().mode().id.c_str());
    CHECK(writeModeRequest(r.dir, "alpha", &err), "write");
    r.reload.pending = true;
    r.run(0.05);
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "normal", "a plain reload drops the override");
    // junk in the file is ignored
    writeFile(modeRequestPath(r.dir), "../../etc/passwd\n");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "normal" && !tu::exists(modeRequestPath(r.dir)), "invalid id ignored");
}

// A daemon that no reload reaches (not under dofscroll.service, issue #338) still takes the tray's request: the one-second
// poll sees the file twice in a row, which a reload that is on its way never allows (it consumes the file within
// milliseconds).
static void testModeRequestWithoutReload() {
    Rig r(std::string(BASE_CONF), {{"alpha.conf", "BUTTON_3 = key:space\n"}});
    r.run(0.1);
    std::string err;
    CHECK(writeModeRequest(r.dir, "alpha", &err), "write request: %s", err.c_str());
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "normal", "a request that was just written waits for its reload");
    r.run(3.0);
    CHECK(r.core->runner().mode().id == "alpha", "the poll applies it without a reload (%s)", r.core->runner().mode().id.c_str());
    CHECK(!tu::exists(modeRequestPath(r.dir)), "request file is consumed");
    // a reload that arrives in time wins and is not undone by the poll
    CHECK(writeModeRequest(r.dir, "normal", &err), "write");
    r.reload.pending = true;
    r.run(3.0);
    CHECK(r.core->runner().mode().id == "normal", "reload path unchanged (%s)", r.core->runner().mode().id.c_str());
}

// Precedence: global < device < mode overrides < app.
static void testModeVsAppPrecedence() {
    Rig r("[global]\nTICK_HZ = 250\nMODE = m\n[app:x]\nZOOM_SENS = 8\n",
          {{"m.conf", "ZOOM_SENS = 1\n"}});
    r.run(0.1);
    CHECK(r.core->settings().zoom.sensitivity == 1, "mode overrides global (%d)", r.core->settings().zoom.sensitivity);
    r.win.cls = "x";
    r.run(0.3);
    CHECK(r.core->settings().zoom.sensitivity == 8, "app beats mode override (%d)", r.core->settings().zoom.sensitivity);
}

// BUTTON_<n> in the main config acts at runtime; a normal.conf file mode applies at startup.
static void testConfigButtonsAndNormalFile() {
    Rig r(std::string(BASE_CONF) + "BUTTON_2 = rightclick\n[app:x]\nBUTTON_2 = none\n");
    r.run(0.1);
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    r.src.pushButton(0, 1, false);
    r.run(0.1);
    CHECK(r.rec.mouse.size() == 2 && r.rec.mouse[0] == "+3", "[global] BUTTON_2 right click");
    r.rec.mouse.clear();
    r.win.cls = "x";
    r.run(0.3);
    r.src.pushButton(0, 1, true);
    r.run(0.1);
    r.src.pushButton(0, 1, false);
    r.run(0.1);
    CHECK(r.rec.mouse.empty(), "app section unbinds");

    Rig n(BASE_CONF, {{"normal.conf", "BUTTON_1 = click\n"}});
    n.run(0.1);
    n.src.pushButton(0, 0, true);
    n.run(0.1);
    n.src.pushButton(0, 0, false);
    n.run(0.1);
    CHECK(!n.rec.mouse.empty() && n.rec.mouse[0] == "+1", "normal.conf applies at startup");
}

// buttonmode:<id>: expanded at runtime, same button mode on several buttons, unknown ids do nothing,
// edits are picked up, the mode file still wins over the profile, the config stays as written.
static void testButtonModes() {
    const std::string conf = std::string(BASE_CONF) + "BUTTON_1 = buttonmode:rc\nBUTTON_2 = buttonmode:rc\n"
                             "BUTTON_3 = buttonmode:nope\n";
    Rig r(conf);
    r.writeButtonMode("rc.conf", "NAME = Right\nACTION = rightclick\n");
    r.reload.pending = true;
    r.run(0.1);
    auto press = [&](int b) {
        r.rec.mouse.clear();
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    press(0);
    CHECK(!r.rec.mouse.empty() && r.rec.mouse[0] == "+3", "button mode in the profile (right click)");
    press(1);
    CHECK(!r.rec.mouse.empty() && r.rec.mouse[0] == "+3", "the same button mode on a second button");
    press(2);
    CHECK(r.rec.mouse.empty(), "unknown button mode: the button does nothing");

    // editing the file is picked up without a reload request (directory stamp)
    r.writeButtonMode("rc.conf", "NAME = Left\nACTION = click\n");
    r.run(1.3);
    press(0);
    CHECK(!r.rec.mouse.empty() && r.rec.mouse[0] == "+1", "edited button mode is picked up (%s)",
          r.rec.mouse.empty() ? "nothing" : r.rec.mouse[0].c_str());
    // a button mode created later resolves the formerly unknown id
    r.writeButtonMode("nope.conf", "ACTION = middleclick\n");
    r.run(1.3);
    press(2);
    CHECK(!r.rec.mouse.empty() && r.rec.mouse[0] == "+2", "a new button mode file is picked up");

    // the expansion is runtime only: the config file is untouched
    std::ifstream f(r.conf);
    std::stringstream ss;
    ss << f.rdbuf();
    CHECK(ss.str() == conf, "the config keeps buttonmode:<id>");

    // a mode file binding wins over the profile; a button mode also works inside a mode file
    Rig m(std::string(BASE_CONF) + "MODE = alpha\nBUTTON_1 = buttonmode:rc\n",
          {{"alpha.conf", "BUTTON_1 = click\nBUTTON_2 = buttonmode:rc\nBUTTON_3 = buttonmode:gone\n"}});
    m.writeButtonMode("rc.conf", "ACTION = rightclick\n");
    m.reload.pending = true;
    m.run(0.1);
    m.src.pushButton(0, 0, true);
    m.run(0.1);
    m.src.pushButton(0, 0, false);
    m.run(0.1);
    CHECK(!m.rec.mouse.empty() && m.rec.mouse[0] == "+1", "the mode file binding wins over buttonmode: of the profile");
    m.rec.mouse.clear();
    m.src.pushButton(0, 1, true);
    m.run(0.1);
    m.src.pushButton(0, 1, false);
    m.run(0.1);
    CHECK(!m.rec.mouse.empty() && m.rec.mouse[0] == "+3", "buttonmode: inside a mode file");
    m.rec.mouse.clear();
    m.src.pushButton(0, 2, true);
    m.run(0.1);
    m.src.pushButton(0, 2, false);
    m.run(0.1);
    CHECK(m.rec.mouse.empty(), "unknown button mode in a mode file: nothing");

    // a button mode with toggle:nav works through the expansion as well
    Rig n(std::string(BASE_CONF) + "BUTTON_1 = buttonmode:latch\n");
    n.writeButtonMode("latch.conf", "ACTION = toggle:nav\n");
    n.reload.pending = true;
    n.run(0.1);
    n.src.pushButton(0, 0, true);
    n.run(0.1);
    n.src.pushButton(0, 0, false);
    n.run(0.1);
    CHECK(n.core->runner().navLatched(), "buttonmode with toggle:nav latches navigation");
}

static void testLogSanitized() {
    std::ostringstream cap;
    std::streambuf *old = std::cerr.rdbuf(cap.rdbuf());
    {
        Rig r(std::string("[global]\n\x1b]0;PWN\x07=1\nSCROLL_V_SENS = 2\n"),
              {{"m.conf", "\x1b[2J=1\nENABLED = 0\n"}});
    }
    std::cerr.rdbuf(old);
    const std::string out = cap.str();
    CHECK(out.find("PWN") != std::string::npos, "config warning logged");
    CHECK(out.find('\x1b') == std::string::npos && out.find('\x07') == std::string::npos,
          "control characters must not reach the log");
    CHECK(out.find("ENABLED is not allowed") != std::string::npos, "mode ENABLED rejection logged");
}

// A raw HID source that finds the device but gets no report, or is refused: the status file says so.
static void testInputIssue() {
    {  // silent: only after kSilentInputNs without any event, and only for HID sources
        Rig r(BASE_CONF);
        r.src.setName("hid");
        r.run(1.0);
        CHECK(r.status.back().find("input_issue=") == std::string::npos, "no hint right after the start");
        r.now += kSilentInputNs;
        r.run(0.1);
        CHECK(r.status.back().find("input_issue=silent\n") != std::string::npos, "silent hint missing");
        r.src.pushMotion(0, 10, 0, 0, 0, 0, 0);  // one report: the hint goes away
        r.run(0.1);
        CHECK(r.status.back().find("input_issue=") == std::string::npos, "hint stays after a report");
    }
    {  // other sources (spacenavd, fake) never get the hint
        Rig r(BASE_CONF);
        r.run(1.0);
        r.now += kSilentInputNs;
        r.run(0.1);
        CHECK(r.status.back().find("input_issue=") == std::string::npos, "hint for a non-HID source");
    }
    {  // blocked: at once
        Rig r(BASE_CONF);
        r.src.setName("iokit");
        r.src.setBlocked(true);
        r.run(0.1);
        CHECK(r.status.back().find("input_issue=blocked\n") != std::string::npos, "blocked missing");
    }
}

// Split layout: the daemon reads devices/ and apps/, reloads on a changed file (stamp poll, debounced) and on a
// reload request, and a broken profile file drops only its own profile. A legacy section is shadowed by a file.
static void testSplitLayoutReload() {
    Rig r("[global]\nTICK_HZ = 250\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 0\n[device:Test Mouse]\nZOOM_SENS = 1\n");
    r.run(0.1);
    CHECK(!r.core->settings().click_button_enabled, "start: click off");
    CHECK(r.core->settings().zoom.sensitivity == 1, "legacy section active");
    std::filesystem::create_directories(r.dir + "/devices");
    std::filesystem::create_directories(r.dir + "/apps");
    writeFile(r.dir + "/devices/mouse.conf", "[device:Test Mouse]\nCLICK_BUTTON_ENABLED = 1\nZOOM_SENS = 7\n");
    writeFile(r.dir + "/apps/broken.conf", "[device:Wrong]\n");
    r.run(0.5);
    CHECK(!r.core->settings().click_button_enabled, "no reload before the poll has seen the change twice");
    r.run(3.0);  // poll sees the change, the next poll confirms it (debounce)
    CHECK(r.core->settings().click_button_enabled, "file in devices/ applied by the stamp poll");
    CHECK(r.core->settings().zoom.sensitivity == 7, "the file wins over the legacy section");
    bool fatal = false, shadowed = false;
    for (const FileDiag &d : r.core->profileDiagnostics()) {
        if (d.origin == FileDiag::PROFILE_FILE && d.fatal) fatal = true;
        if (d.origin == FileDiag::LEGACY_SECTION && d.fatal) shadowed = true;
    }
    CHECK(fatal && shadowed, "diagnostics: broken file and shadowed section");
    // reload request (SIGHUP, event, socket) applies at once
    writeFile(r.dir + "/devices/mouse.conf", "[device:Test Mouse]\nCLICK_BUTTON_ENABLED = 1\nZOOM_SENS = 9\n");
    r.reload.pending = true;
    r.run(0.02);
    CHECK(r.core->settings().zoom.sensitivity == 9, "reload request re-reads the profile files");
    // removing the file brings the legacy section back
    std::filesystem::remove(r.dir + "/devices/mouse.conf");
    r.reload.pending = true;
    r.run(0.02);
    CHECK(r.core->settings().zoom.sensitivity == 1 && !r.core->settings().click_button_enabled, "file removed");
}

int main() {
    testInputIssue();
    testSplitLayoutReload();
    testButtonModes();
    testLogSanitized();
    testModeVsAppPrecedence();
    testConfigButtonsAndNormalFile();
    testModeSwitchButton();
    testModePrev();
    testModeCycleList();
    testModeCycleBuiltins();
    testHoldMode();
    testModeNamedRelease();
    testToggleButtons();
    testMotion();
    testStaleWatchdog();
    testLiveFeedSink();
    testButtonBinding();
    testReload();
    testInputMode();
    testModeRequest();
    testModeRequestWithoutReload();
    testAutoReload();
    testDeviceAndWindowProfiles();
    testProgramLayerSwitch();
    testProgramLayerReload();
    testDeviceRemovedReleasesState();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("daemon core tests passed");
    return 0;
}
