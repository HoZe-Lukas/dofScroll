// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Several SpaceMice at once (#93): one lane per device with its own profile, joystick mode, holds and
// toggles; one shared output that counts who holds a key. Driven by a fake source with two devices.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "daemon_core.hpp"
#include "output_mux.hpp"
#include "platform/fake_input.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct MdRec {
    int wv = 0, wh = 0, ctrl_down = 0, ctrl_up = 0;
    bool ctrl = false;
    std::vector<std::string> taps;
};

struct MdOut : Output {
    MdRec *r;
    explicit MdOut(MdRec *rec) : r(rec) {}
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { r->wv += n; }
    void wheelH(int n) override { r->wh += n; }
    void setKey(Key k, bool d) override {
        if (k != Key::Ctrl) return;
        if (d && !r->ctrl) ++r->ctrl_down;
        if (!d && r->ctrl) ++r->ctrl_up;
        r->ctrl = d;
    }
    void tapKey(Key k) override { r->taps.push_back(keyName(k)); }
    void mouseButton(int, bool) override {}
    void releaseAll() override { r->ctrl = false; }
    void flush() override {}
};

struct MdWindow : ActiveWindowProvider {
    bool init() override { return true; }
    std::string poll() override { return std::string(); }
};
struct MdReload : ReloadListener {
    bool init(std::string *) override { return true; }
    bool pollReload() override { return false; }
};
struct MdSource : FakeInputSource {
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> devices() const override { return list; }
};

static void writeFile(const std::string &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    f << text;
}

static DeviceInfo dev(int id, const char *name) {
    DeviceInfo d;
    d.id = id;
    d.name = name;
    d.reportedButtons = 2;
    return d;
}

struct MdRig {
    std::string dir;
    MdRec rec;
    MdSource src;
    MdWindow win;
    MdReload reload;
    int64_t now = 10'000'000'000;
    std::unique_ptr<DaemonCore> core;
    std::vector<std::string> status;

    MdRig(const std::string &conf, std::vector<DeviceInfo> devs) : dir(tu::makeTempDir("dofscroll-multi-test")) {
        writeFile(dir + "/dofscroll.conf", conf);
        src.list = std::move(devs);
        DaemonDeps deps;
        deps.input = &src;
        deps.activeWindow = &win;
        deps.reload = &reload;
        deps.paths.configDir = dir;
        deps.paths.runtimeDir = dir;
        MdRec *rp = &rec;
        deps.makeOutput = [rp](const std::string &, std::string *) { return std::unique_ptr<Output>(new MdOut(rp)); };
        deps.clock = [this] { return now; };
        deps.writeStatus = [this](const std::string &body, int64_t) { status.push_back(body); };
        core = std::make_unique<DaemonCore>(std::move(deps));
        assert(core->initialize());
        assert(core->reader().connect());
    }
    ~MdRig() { tu::removeAll(dir); }
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
    void unplug(int id) {
        for (size_t i = 0; i < src.list.size(); ++i)
            if (src.list[i].id == id) {
                src.list.erase(src.list.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        InputEvent ev;
        ev.type = InputEventType::DevicesChanged;
        src.push(ev);
    }
};

static const char *const BASE = "[global]\nTICK_HZ = 250\nCLICK_BUTTON_ENABLED = 0\n";
// Same peak vectors as test_daemon_core: a vertical push scrolls, a twist zooms (Ctrl + wheel).
static void push(MdRig &r, int dev) { r.src.pushMotion(dev, 69, 286, 350, 145, 56, 155); }
static void twist(MdRig &r, int dev) { r.src.pushMotion(dev, 82, 144, 90, 35, 350, 111); }
static void idle(MdRig &r, int dev) { r.src.pushMotion(dev, 0, 0, 0, 0, 0, 0); }
// The deflection counts as stale after 250 ms without an event: repeat it like a real device does.
// dev1 / dev2: 'p' push, 't' twist, '0' idle, ' ' no event.
static void hold(MdRig &r, double secs, char dev1, char dev2 = ' ') {
    for (int i = 0; i < static_cast<int>(secs / 0.05); ++i) {
        for (int d = 1; d <= 2; ++d) {
            const char c = d == 1 ? dev1 : dev2;
            if (c == 'p') push(r, d);
            else if (c == 't') twist(r, d);
            else if (c == '0') idle(r, d);
        }
        r.run(0.05);
    }
}

// The mux alone: shared keys are released when the last holder lets go; a view's releaseAll leaves the others.
static void testMux() {
    MdRec rec;
    MdOut real(&rec);
    OutputMux mux(&real);
    Output *a = mux.addView();
    Output *b = mux.addView();
    a->setKey(Key::Ctrl, true);
    b->setKey(Key::Ctrl, true);
    CHECK(rec.ctrl && rec.ctrl_down == 1, "Ctrl pressed once for two holders");
    a->releaseAll();
    CHECK(rec.ctrl && rec.ctrl_up == 0, "Ctrl stays while the other view holds it");
    CHECK(!mux.viewHolds(a) && mux.viewHolds(b), "holder bookkeeping");
    b->setKey(Key::Ctrl, false);
    CHECK(!rec.ctrl && rec.ctrl_up == 1, "Ctrl released by the last holder");
    a->setKey(Key::Ctrl, true);
    mux.releaseEverything();
    CHECK(!rec.ctrl, "releaseEverything clears the real output");
    a->setKey(Key::Ctrl, true);
    CHECK(rec.ctrl, "a key can be pressed again after releaseEverything");
    mux.removeView(a);
    CHECK(!rec.ctrl, "removing a view releases what it held");
    Output *c = mux.addView();
    CHECK(c != nullptr, "a freed view slot is reused");
    for (int i = 2; i < OutputMux::kMaxViews; ++i) CHECK(mux.addView() != nullptr, "view %d", i);
    CHECK(mux.addView() == nullptr, "no view beyond kMaxViews");
}

static void testTwoLanesAndNames() {
    MdRig r(BASE, {dev(1, "Mouse A"), dev(2, "Mouse B")});
    r.run(0.1);
    CHECK(r.core->laneCount() == 2 && r.core->lane(0).device == "Mouse A" && r.core->lane(1).device == "Mouse B", "two lanes, plain names");
    // Two of the same model: only the first gets a lane.
    MdRig r3(BASE, {dev(1, "Same"), dev(2, "Same")});
    r3.run(0.1);
    CHECK(r3.core->laneCount() == 1 && r3.core->deviceName() == "Same", "second identical model ignored (%zu lanes)", r3.core->laneCount());
    // A Pro and a Compact next to a second Compact: two lanes.
    MdRig r5(BASE, {dev(1, "Pro"), dev(2, "Compact"), dev(3, "Compact")});
    r5.run(0.1);
    CHECK(r5.core->laneCount() == 2 && r5.core->lane(1).device == "Compact", "one lane per model (%zu)", r5.core->laneCount());
    MdRig r4(BASE, {dev(1, "Only")});
    r4.run(0.1);
    CHECK(r4.core->laneCount() == 1 && r4.core->deviceName() == "Only", "single device: plain name, one lane");
}

static void testIndependentProfilesAndModes() {
    // Device 2 gets the media mode (keys) by its device profile, device 1 stays normal.
    MdRig r(std::string(BASE) + "\n[device:Mouse B]\nMODE = media\n", {dev(1, "Mouse A"), dev(2, "Mouse B")});
    r.run(0.1);
    CHECK(r.core->lane(0).runner->mode().id == "normal", "lane 1 mode %s", r.core->lane(0).runner->mode().id.c_str());
    CHECK(r.core->lane(1).runner->mode().id == "media", "lane 2 mode %s", r.core->lane(1).runner->mode().id.c_str());
    // Device 1 pushes: wheel. Device 2 pushes: arrow keys, no wheel.
    hold(r, 0.6, 'p');
    CHECK(r.rec.wv != 0 && r.rec.taps.empty(), "device 1 scrolls (wv=%d, taps=%zu)", r.rec.wv, r.rec.taps.size());
    hold(r, 0.3, '0');
    const int wv = r.rec.wv;
    hold(r, 0.6, ' ', 'p');
    CHECK(r.rec.wv == wv && !r.rec.taps.empty(), "device 2 presses keys only (wv=%d, taps=%zu)", r.rec.wv, r.rec.taps.size());
    CHECK(r.status.back().find("device2=Mouse B") != std::string::npos && r.status.back().find("mode2=media") != std::string::npos &&
              r.status.back().find("lanes=2") != std::string::npos,
          "status lists the second device: %s", r.status.back().c_str());
}

static void testButtonSwitchesOnlyOwnLane() {
    MdRig r(std::string(BASE) + "\nBUTTON_1 = mode:timeline\n", {dev(1, "Mouse A"), dev(2, "Mouse B")});
    r.run(0.1);
    r.src.pushButton(2, 0, true);
    r.run(0.1);
    r.src.pushButton(2, 0, false);
    r.run(0.1);
    CHECK(r.core->lane(0).runner->mode().id == "normal", "device 1 untouched: %s", r.core->lane(0).runner->mode().id.c_str());
    CHECK(r.core->lane(1).runner->mode().id == "timeline", "device 2 switched: %s", r.core->lane(1).runner->mode().id.c_str());
}

static void testSharedCtrlAndUnplug() {
    MdRig r(BASE, {dev(1, "Mouse A"), dev(2, "Mouse B")});
    r.run(0.1);
    hold(r, 0.8, 't', 't');
    CHECK(r.rec.ctrl && r.rec.ctrl_down == 1, "both zoom with one Ctrl press (down=%d)", r.rec.ctrl_down);
    hold(r, 0.5, '0', 't');
    CHECK(r.rec.ctrl, "Ctrl stays while device 2 zooms");
    // Unplug device 2 while it is deflected: its lane ends, Ctrl is released, device 1 goes on.
    r.unplug(2);
    r.run(0.2);
    CHECK(r.core->laneCount() == 1, "lane of the removed device ended (%zu)", r.core->laneCount());
    CHECK(!r.rec.ctrl, "Ctrl released after the unplug");
    hold(r, 0.6, 'p');
    CHECK(r.rec.wv != 0, "device 1 still works");
}

static void testUnplugKeepsOtherDeviceState() {
    MdRig r(std::string(BASE) + "\nBUTTON_1 = mode:timeline\n", {dev(1, "Mouse A"), dev(2, "Mouse B")});
    r.run(0.1);
    r.src.pushButton(1, 0, true);
    r.run(0.1);
    r.src.pushButton(1, 0, false);
    r.run(0.1);
    CHECK(r.core->lane(0).runner->mode().id == "timeline", "device 1 in timeline");
    r.unplug(2);
    r.run(0.2);
    CHECK(r.core->laneCount() == 1 && r.core->lane(0).runner->mode().id == "timeline", "device 1 keeps its mode after device 2 left");
    // Plug a new device: new lane, normal mode, device 1 unchanged.
    r.src.list.push_back(dev(3, "Mouse C"));
    InputEvent ev;
    ev.type = InputEventType::DevicesChanged;
    r.src.push(ev);
    r.run(0.2);
    CHECK(r.core->laneCount() == 2 && r.core->lane(1).runner->mode().id == "normal", "new lane for the new device");
    CHECK(r.core->lane(0).runner->mode().id == "timeline", "device 1 unchanged by the hotplug");
}

static void testReplugKeepsLane() {
    MdRig r(std::string(BASE) + "\nBUTTON_1 = mode:timeline\n", {dev(1, "Mouse A")});
    r.run(0.1);
    r.src.pushButton(1, 0, true);
    r.run(0.1);
    r.src.pushButton(1, 0, false);
    r.run(0.1);
    r.unplug(1);
    r.run(0.1);
    r.src.list.push_back(dev(2, "Mouse A"));  // the same mouse back, new id
    InputEvent ev;
    ev.type = InputEventType::DevicesChanged;
    r.src.push(ev);
    r.run(0.2);
    CHECK(r.core->laneCount() == 1, "one lane (%zu)", r.core->laneCount());
}

static void testMergedSourceSingleLane() {
    // spacenavd: one merged device (id 0), events with device 0. Behaves as before.
    MdRig r(BASE, {dev(0, "Merged")});
    for (int i = 0; i < 12; ++i) {
        push(r, 0);
        r.run(0.05);
    }
    CHECK(r.core->laneCount() == 1 && r.rec.wv != 0, "merged source: one lane scrolls");
}

int main() {
    testMux();
    testTwoLanesAndNames();
    testIndependentProfilesAndModes();
    testButtonSwitchesOnlyOwnLane();
    testSharedCtrlAndUnplug();
    testUnplugKeepsOtherDeviceState();
    testReplugKeepsLane();
    testMergedSourceSingleLane();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("multi device tests passed");
    return 0;
}
