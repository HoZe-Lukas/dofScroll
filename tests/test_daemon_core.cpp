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
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~Rig() {
        tu::removeAll(dir);
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

int main() {
    testModeVsAppPrecedence();
    testConfigButtonsAndNormalFile();
    testModeSwitchButton();
    testMotion();
    testStaleWatchdog();
    testButtonBinding();
    testReload();
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
