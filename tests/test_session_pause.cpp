// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Session pause: while the daemon's own session is not the active one it behaves like suspended
// (everything released, no output, status line session=inactive); unknown state keeps working.
// Deterministic: fake input, fake clock, fake session monitor, no threads.
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

struct FakeSession : SessionMonitor {
    bool active = true;
    int polls = 0;
    bool isActive(std::string *d) override {
        ++polls;
        if (d) *d = "fake";
        return active;
    }
};

// Records everything: what is held right now and how much output was produced.
struct SessRec {
    std::set<int> keys;   // currently held keys
    std::set<int> mouse;  // currently held mouse buttons
    int wheels = 0;       // wheelV/wheelH calls
    int taps = 0;         // tapKey calls
    int releaseAlls = 0;
    bool held() const { return !keys.empty() || !mouse.empty(); }
    int output() const { return wheels + taps; }
};

struct SessOutput : Output {
    SessRec *r;
    explicit SessOutput(SessRec *rec) : r(rec) {}
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

struct SessWindow : ActiveWindowProvider {
    std::string cls;
    bool init() override { return true; }
    std::string poll() override { return cls; }
};

struct SessReload : ReloadListener {
    bool pending = false;
    bool init(std::string *) override { return true; }
    bool pollReload() override { bool p = pending; pending = false; return p; }
};

struct SessSource : FakeInputSource {
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> devices() const override { return list; }
};

static void writeFile(const std::string &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    f << text;
}

struct SessRig {
    std::string dir, conf;
    SessRec rec;
    SessSource src;
    SessWindow win;
    SessReload reload;
    bool suspend = false;
    std::string status;
    FakeSession mon;
    int64_t now = 10'000'000'000;
    std::unique_ptr<DaemonCore> core;

    explicit SessRig(const std::string &config,
                 const std::vector<std::pair<std::string, std::string>> &modeFiles = {})
        : dir(tu::makeTempDir("dofscroll-session-test")) {
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
        SessRec *rp = &rec;
        deps.makeOutput = [rp](const std::string &, std::string *) {
            return std::unique_ptr<Output>(new SessOutput(rp));
        };
        deps.clock = [this] { return now; };
        deps.suspendRequested = [this] { return suspend; };
        deps.sessionMonitor = &mon;
        deps.writeStatus = [this](const std::string &b, int64_t) { status = b; };
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~SessRig() { tu::removeAll(dir); }

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
static const char *const CONF = "[global]\nTICK_HZ = 250\nBUTTON_1 = key:up\n";
static const char *const NAV_CONF =
    "[global]\nTICK_HZ = 250\nNAV_BUTTON = 0\nNAV_BUTTON_ENABLED = 1\n";

static void testPauseMidMotion() {
    SessRig r(CONF);
    r.move();
    r.run(0.3);
    CHECK(r.rec.output() > 0, "motion must produce output");
    r.mon.active = false;
    r.run(1.2);  // the session is polled about once per second
    const int before = r.rec.output();
    r.run(1.0);
    CHECK(r.rec.output() == before, "no output while inactive (%d)", r.rec.output() - before);
    CHECK(!r.rec.held(), "nothing held while inactive");
    CHECK(r.status.find("session=inactive") != std::string::npos, "status says inactive: %s", r.status.c_str());
    CHECK(r.status.find("enabled=1") != std::string::npos, "existing keys unchanged");
}

static void testPauseMidHold() {
    SessRig r(CONF);
    r.src.pushButton(0, 0, true);
    r.run(0.2);
    CHECK(r.rec.keys.count(KEY_UP), "button holds the key");
    r.mon.active = false;
    r.run(1.2);
    CHECK(!r.rec.held(), "key released on pause");
    r.src.pushButton(0, 0, false);
    r.run(0.1);
    r.src.pushButton(0, 0, true);
    r.run(0.5);
    CHECK(!r.rec.held(), "button presses ignored while inactive");
}

static void testNavHoldReleased() {
    SessRig r(NAV_CONF);
    r.src.pushButton(0, 0, true);
    r.move();
    r.run(0.3);
    r.mon.active = false;
    r.run(1.2);
    const int before = r.rec.output();
    r.run(1.0);
    CHECK(r.rec.output() == before && !r.rec.held(), "nav hold silenced");
}

static void testResumeNoStaleReplay() {
    SessRig r(CONF);
    r.move();
    r.run(0.3);
    r.mon.active = false;
    r.run(1.2);
    r.stop();  // the mouse is released while another session is in front
    r.run(0.3);
    r.mon.active = true;
    r.run(1.2);
    const int before = r.rec.output();
    r.run(1.0);
    CHECK(r.rec.output() == before, "no stale motion replayed after resume (%d)", r.rec.output() - before);
    CHECK(r.status.find("session=active") != std::string::npos, "status active again");
    r.move();
    r.run(0.3);
    CHECK(r.rec.output() > before, "output works again after resume");
}

static void testNoMonitorKeepsWorking() {
    // No monitor at all (the default of the core): never paused, no session polling.
    SessRig r(CONF);
    r.core.reset();
    DaemonDeps deps;
    deps.input = &r.src;
    deps.activeWindow = &r.win;
    deps.reload = &r.reload;
    deps.paths.configDir = r.dir;
    deps.paths.runtimeDir = r.dir;
    SessRec *rp = &r.rec;
    deps.makeOutput = [rp](const std::string &, std::string *) {
        return std::unique_ptr<Output>(new SessOutput(rp));
    };
    SessRig *rig = &r;
    deps.clock = [rig] { return rig->now; };
    r.core = std::make_unique<DaemonCore>(std::move(deps));
    assert(r.core->initialize());
    assert(r.core->reader().connect());
    r.move();
    r.run(2.5);
    CHECK(r.rec.output() > 0, "works without a monitor");
}

static void testPollRate() {
    SessRig r(CONF);
    r.run(5.0);
    CHECK(r.mon.polls >= 4 && r.mon.polls <= 6, "about once per second, got %d in 5 s", r.mon.polls);
}

static void testFlapping() {
    SessRig r(CONF);
    r.move();
    for (int i = 0; i < 12; ++i) {
        r.mon.active = (i % 2) == 0;
        r.run(1.05);
    }
    r.mon.active = false;
    r.run(1.2);
    CHECK(!r.rec.held(), "nothing held after flapping ended inactive");
    const int before = r.rec.output();
    r.run(0.5);
    CHECK(r.rec.output() == before, "silent after flapping ends inactive");
    r.mon.active = true;
    r.stop();
    r.run(1.2);
    r.expectQuiet("after flapping");
}

int main() {
    testPauseMidMotion();
    testPauseMidHold();
    testNavHoldReleased();
    testResumeNoStaleReplay();
    testNoMonitorKeepsWorking();
    testPollRate();
    testFlapping();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("session pause: ok");
    return 0;
}
