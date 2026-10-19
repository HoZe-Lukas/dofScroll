// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Drives the platform-independent daemon core with a scripted input source, a recording
// output, a fake active-window provider and a fake reload channel (no OS, no threads).
#undef NDEBUG
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
#include "mode_request.hpp"
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

static void testModeSwitchButton() {
    // ids: alpha < normal < off < timeline (built-ins: normal, off, timeline)
    Rig r(std::string(BASE_CONF) + "MODE = alpha\n",
          {{"alpha.conf", "BUTTON_2 = mode:next\nBUTTON_3 = key:space\n"},
           {"zed.conf", "BUTTON_2 = mode:next\nBUTTON_4 = mode:off\n"}});
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "alpha", "starts in the configured mode");
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
    CHECK(r.core->runner().mode().id == "normal", "next after alpha is normal (got %s)",
          r.core->runner().mode().id.c_str());
    r.src.pushButton(0, 1, false);
    r.src.pushButton(0, 2, false);
    r.run(0.1);
    CHECK(statusMode(r) == "normal", "status shows the effective mode (%s)", statusMode(r).c_str());
    // unrelated profile re-resolution (app change) keeps the override
    r.win.cls = "navigator firefox";
    r.run(0.3);
    CHECK(r.core->runner().mode().id == "normal", "override is sticky across profile changes");
    // normal has no binding: the user cannot cycle further from there. Reload clears the override.
    writeFile(r.dir + "/modes/alpha.conf", "BUTTON_2 = mode:next\n");
    r.reload.pending = true;
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "alpha", "reload clears the override (%s)", r.core->runner().mode().id.c_str());
    // cycle through all modes and wrap: alpha -> normal; use zed to wrap by going to last
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = zed\n");
    r.run(0.05);
    CHECK(r.core->runner().mode().id == "zed", "zed active");
    press(1);  // next wraps around from the last id
    CHECK(r.core->runner().mode().id == "alpha", "next wraps to the first id");
    press(1);  // next -> normal
    CHECK(r.core->runner().mode().id == "normal", "alpha -> normal");
    CHECK(!r.rec.ctrl, "no stray state");
    r.reload.pending = true;  // back to zed
    r.run(0.05);
    r.src.pushButton(0, 3, true);
    r.run(0.1);
    CHECK(r.core->runner().mode().id == "off", "fixed target mode:off");
}

// mode:prev is the mirror of mode:next: alphabetical ids, wrapping in both directions.
static void testModePrev() {
    // ids: alpha < normal < off < timeline < zed (built-ins: normal, off, timeline)
    Rig r(std::string(BASE_CONF) + "MODE = alpha\n",
          {{"alpha.conf", "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n"},
           {"zed.conf", "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n"}});
    r.run(0.1);
    auto press = [&](int b) {
        r.src.pushButton(0, b, true);
        r.run(0.1);
        r.src.pushButton(0, b, false);
        r.run(0.1);
    };
    auto id = [&] { return r.core->runner().mode().id; };
    CHECK(id() == "alpha", "starts in alpha");
    press(0);  // prev from the first id wraps to the last
    CHECK(id() == "zed", "prev wraps from the first to the last (%s)", id().c_str());
    press(0);
    CHECK(id() == "timeline", "prev goes to the previous id (%s)", id().c_str());
    // timeline has no bindings of its own: continue from a mode that has them (reload, start at zed)
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = zed\n");
    r.run(0.05);
    CHECK(id() == "zed", "zed active after the reload");
    press(1);  // next from the last id wraps to the first
    CHECK(id() == "alpha", "next wraps from the last to the first (%s)", id().c_str());
    press(1);  // next -> normal
    CHECK(id() == "normal", "alpha -> normal");
    // next followed by prev returns to the start
    r.reload.pending = true;
    writeFile(r.conf, std::string(BASE_CONF) + "MODE = alpha\n");
    r.run(0.05);
    press(1);
    CHECK(id() == "normal", "next from alpha");
    // (normal has no binding: go back through the file mode instead)
    r.reload.pending = true;
    r.run(0.05);
    press(0);
    press(1);
    CHECK(id() == "alpha", "prev then next returns to the start (%s)", id().c_str());
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

int main() {
    testButtonModes();
    testLogSanitized();
    testModeVsAppPrecedence();
    testConfigButtonsAndNormalFile();
    testModeSwitchButton();
    testModePrev();
    testHoldMode();
    testToggleButtons();
    testMotion();
    testStaleWatchdog();
    testLiveFeedSink();
    testButtonBinding();
    testReload();
    testModeRequest();
    testAutoReload();
    testDeviceAndWindowProfiles();
    testDeviceRemovedReleasesState();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("daemon core tests passed");
    return 0;
}
