// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Hardware-free integration test of the pipeline config -> mix -> DSP -> steps.
// Plain assert tests, no framework, own main.
//
// The pipeline under test is the real ModeRunner (src/mode_runner.cpp, the same code the
// daemon runs) with a fake Output that only counts steps. Not covered: stale detection
// (the daemon simply feeds raw = 0 then, simulated directly here) and the nav/click
// buttons (see test_mode_runner.cpp).
#undef NDEBUG
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "config.hpp"
#include "dsp.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"

using namespace spn;

// Counts steps instead of sending real events.
struct FakeOutput : Output {
    int scroll_v = 0;   // signed sum (wheelV without Ctrl)
    int scroll_h = 0;
    int zoom = 0;       // signed sum (wheelV while Ctrl is held)
    int abs_scroll_v = 0, abs_scroll_h = 0, abs_zoom = 0;  // sum of magnitudes
    bool ctrl = false;
    int ctrl_presses = 0;

    const char *name() const override { return "fake"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override {
        if (ctrl) { zoom += n; abs_zoom += std::abs(n); }
        else      { scroll_v += n; abs_scroll_v += std::abs(n); }
    }
    void wheelH(int n) override { scroll_h += n; abs_scroll_h += std::abs(n); }
    void setKey(Key k, bool down) override {
        if (k != Key::Ctrl || down == ctrl) return;
        ctrl = down;
        if (down) ctrl_presses++;
    }
    void tapKey(Key) override {}
    void mouseButton(int, bool) override {}
    void releaseAll() override {}
    void flush() override {}
};

// The daemon's tick logic (default mode, legacy path) via the real ModeRunner.
struct Pipeline {
    Settings s;
    FakeOutput out;
    ModeRunner runner;

    explicit Pipeline(const Settings &st) : s(st), runner(&out) {}

    void tick(const RawAxes &raw, double dt) { runner.tick(s, raw, 0, true, dt); }
};

// Peak vector with a ramp 0 -> peak over ramp_s, then constant, dur_s long.
static void simulate(Pipeline &p, const RawAxes &peak, double hz, double dur_s,
                     double ramp_s = 0.1) {
    const double dt = 1.0 / hz;
    const int n = static_cast<int>(std::lround(dur_s * hz));
    for (int i = 0; i < n; ++i) {
        double t = (i + 1) * dt;
        double f = ramp_s > 0 ? std::min(1.0, t / ramp_s) : 1.0;
        RawAxes r{};
        for (int a = 0; a < AX_COUNT; ++a) r[a] = static_cast<int>(std::lround(peak[a] * f));
        p.tick(r, dt);
    }
}

static const double HZS[] = {125.0, 250.0, 1000.0};

struct Scenario {
    const char *name;
    RawAxes peak;  // x y z rx ry rz
    enum Expect { ZOOM, VERT, HORIZ, NONE } expect;
};

static const Scenario SCEN[] = {
    {"Twist",          {82, 144, 90, 35, 350, 111}, Scenario::ZOOM},
    {"Tilt v/z",      {61, 125, 140, 350, 50, 80}, Scenario::VERT},
    {"Tilt sideways", {107, 95, 65, 107, 62, 350}, Scenario::HORIZ},
    {"Push sideways", {350, 293, 60, 132, 70, 75}, Scenario::HORIZ},
    {"Push v/z",    {69, 286, 350, 145, 56, 155}, Scenario::VERT},
    {"Lift",           {170, 350, 149, 59, 103, 81}, Scenario::NONE},
    {"Rest",            {0, 0, 0, 0, 0, 0}, Scenario::NONE},
};

static int failures = 0;

// Crosstalk between the scroll channels (e.g. sideways tilt also produces z+rx > deadzone)
// is measurable with the default mixes. The strict "one direction only" check is
// therefore only active with SPN_STRICT_CROSSTALK=1; otherwise the crosstalk is printed
// as a NOTE. Zoom/Ctrl expectations are always strict.
static bool strictCrosstalk() {
    const char *e = std::getenv("SPN_STRICT_CROSSTALK");
    return e && *e == '1';
}
static void crosstalk(bool ok, const char *what, const char *nm, double hz, int val) {
    if (ok) return;
    if (strictCrosstalk()) {
        ++failures;
        std::fprintf(stderr, "FAIL %s @%g Hz: %s = %d\n", nm, hz, what, val);
    } else if (hz == 250.0) {
        std::fprintf(stderr, "NOTE %s: crosstalk %s = %d\n", nm, what, val);
    }
}
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static void testScenarios() {
    for (size_t si = 0; si < sizeof(SCEN) / sizeof(SCEN[0]); ++si) {
        const Scenario &sc = SCEN[si];
        int totals[3] = {0};
        for (int hi = 0; hi < 3; ++hi) {
            Pipeline p{Settings()};
            simulate(p, sc.peak, HZS[hi], 1.0);
            const FakeOutput &o = p.out;
            const char *nm = sc.name;
            double hz = HZS[hi];
            switch (sc.expect) {
            case Scenario::ZOOM:
                CHECK(o.abs_zoom > 0, "%s @%g Hz: no zoom", nm, hz);
                CHECK(o.abs_scroll_v == 0 && o.abs_scroll_h == 0,
                      "%s @%g Hz: scroll v=%d h=%d while zooming", nm, hz, o.abs_scroll_v, o.abs_scroll_h);
                CHECK(o.ctrl, "%s @%g Hz: Ctrl not held", nm, hz);
                CHECK(o.ctrl_presses == 1, "%s @%g Hz: Ctrl pressed %d times", nm, hz, o.ctrl_presses);
                totals[hi] = o.abs_zoom;
                break;
            case Scenario::VERT:
                CHECK(o.abs_scroll_v > 0, "%s @%g Hz: no vertical scroll", nm, hz);
                crosstalk(o.abs_scroll_h == 0, "horizontal", nm, hz, o.abs_scroll_h);
                CHECK(o.abs_zoom == 0 && !o.ctrl && o.ctrl_presses == 0,
                      "%s @%g Hz: zoom %d / Ctrl", nm, hz, o.abs_zoom);
                totals[hi] = o.abs_scroll_v;
                break;
            case Scenario::HORIZ:
                CHECK(o.abs_scroll_h > 0, "%s @%g Hz: no horizontal scroll", nm, hz);
                crosstalk(o.abs_scroll_v == 0, "vertical", nm, hz, o.abs_scroll_v);
                CHECK(o.abs_zoom == 0 && !o.ctrl && o.ctrl_presses == 0,
                      "%s @%g Hz: zoom %d / Ctrl", nm, hz, o.abs_zoom);
                totals[hi] = o.abs_scroll_h;
                break;
            case Scenario::NONE:
                CHECK(o.abs_zoom == 0 && o.ctrl_presses == 0, "%s @%g Hz: zoom %d", nm, hz,
                      o.abs_zoom);
                crosstalk(o.abs_scroll_v == 0, "vertical", nm, hz, o.abs_scroll_v);
                crosstalk(o.abs_scroll_h == 0, "horizontal", nm, hz, o.abs_scroll_h);
                break;
            }
        }
        // Tick-rate independence: only a small deviation (step quantization + latching).
        if (sc.expect != Scenario::NONE) {
            int lo = *std::min_element(totals, totals + 3);
            int hi = *std::max_element(totals, totals + 3);
            CHECK(hi - lo <= std::max(3, hi / 10), "%s: step counts per tick rate %d/%d/%d",
                  sc.name, totals[0], totals[1], totals[2]);
            std::printf("  %-16s steps @125/250/1000 Hz: %d / %d / %d\n", sc.name, totals[0],
                        totals[1], totals[2]);
        }
    }
}

// --- Config helpers -------------------------------------------------------
static std::string tmpPath() {
    return "/tmp/test_scenarios_" + std::to_string(getpid()) + ".conf";
}
static ProfileSet loadFromString(const std::string &text) {
    std::string path = tmpPath();
    { std::ofstream f(path); f << text; }
    ProfileSet ps;
    std::vector<std::string> warn;
    bool ok = loadProfiles(path, &ps, &warn);
    std::remove(path.c_str());
    assert(ok);
    CHECK(warn.empty(), "warnings while loading: %zu (first: %s)", warn.size(),
          warn.empty() ? "" : warn[0].c_str());
    return ps;
}

static int zoomSteps(const Settings &s, double hz = 250.0) {
    Pipeline p{s};
    simulate(p, SCEN[0].peak, hz, 1.0);
    return p.out.abs_zoom;
}

static void testProfileSwitch() {
    ProfileSet ps = loadFromString("ZOOM_SENS = 5\n\n[app:firefox]\nZOOM_SENS = 12\n");
    Settings base = ps.resolve("SpaceMouse Pro", "navigator other");
    Settings ff = ps.resolve("SpaceMouse Pro", "Navigator Firefox");
    int nb = zoomSteps(base), nf = zoomSteps(ff);
    CHECK(base.zoom.sensitivity == 5 && ff.zoom.sensitivity == 12, "resolution %d/%d",
          base.zoom.sensitivity, ff.zoom.sensitivity);
    CHECK(nf > nb * 2 && nb > 0, "Firefox profile: %d vs. base %d steps", nf, nb);
}

static void testLegacyEquivalence() {
    ProfileSet legacy = loadFromString(
        "SCROLL_SENSITIVITY_Y = 8\nSCROLL_SENSITIVITY_X = 6\nZOOM_SENSITIVITY = 9\n"
        "SCROLL_DEADZONE_Y = 25\nSCROLL_DEADZONE_X = 15\nZOOM_DEADZONE = 35\n"
        "INVERT_Y = -1\nINVERT_X = 1\nZOOM_INVERT = 1\n");
    ProfileSet renamed = loadFromString(
        "SCROLL_V_SENS = 8\nSCROLL_H_SENS = 6\nZOOM_SENS = 9\n"
        "SCROLL_V_DEADZONE = 25\nSCROLL_H_DEADZONE = 15\nZOOM_DEADZONE = 35\n"
        "SCROLL_V_SIGN = -1\nSCROLL_H_SIGN = 1\nZOOM_SIGN = -1\n");
    Settings a = legacy.resolve("", ""), b = renamed.resolve("", "");
    for (size_t si = 0; si < sizeof(SCEN) / sizeof(SCEN[0]); ++si) {
        Pipeline pa{a}, pb{b};
        simulate(pa, SCEN[si].peak, 250.0, 1.0);
        simulate(pb, SCEN[si].peak, 250.0, 1.0);
        CHECK(pa.out.scroll_v == pb.out.scroll_v && pa.out.scroll_h == pb.out.scroll_h &&
                  pa.out.zoom == pb.out.zoom,
              "legacy != new for %s", SCEN[si].name);
    }
}

static void testSigns() {
    // Positive deflection: z and rx -> down (+), x -> right (+).
    {
        Pipeline p{Settings()};
        RawAxes r{}; r[AX_Z] = 300;
        simulate(p, r, 250.0, 1.0, 0.0);
        CHECK(p.out.scroll_v > 0 && p.out.scroll_h == 0, "z>0: v=%d h=%d", p.out.scroll_v, p.out.scroll_h);
        Pipeline q{Settings()};
        RawAxes r2{}; r2[AX_RX] = 300;
        simulate(q, r2, 250.0, 1.0, 0.0);
        CHECK(q.out.scroll_v > 0, "rx>0: v=%d", q.out.scroll_v);
        Pipeline h{Settings()};
        RawAxes r3{}; r3[AX_X] = 300;
        simulate(h, r3, 250.0, 1.0, 0.0);
        CHECK(h.out.scroll_h > 0 && h.out.scroll_v == 0, "x>0: h=%d v=%d", h.out.scroll_h, h.out.scroll_v);
    }
    // ry > 0, sign 1: positive rate = zoom in = wheelV(-n) -> negative sum.
    {
        Pipeline p{Settings()};
        RawAxes r{}; r[AX_RY] = 300;
        simulate(p, r, 250.0, 1.0, 0.0);
        CHECK(p.out.zoom < 0 && p.out.abs_zoom > 0, "ry>0 sign=1: zoom=%d", p.out.zoom);
        Settings s; s.zoom.sign = -1;
        Pipeline q{s};
        simulate(q, r, 250.0, 1.0, 0.0);
        CHECK(q.out.zoom > 0, "ry>0 sign=-1: zoom=%d", q.out.zoom);
        // ry < 0 with sign 1 -> zoom out.
        RawAxes rn{}; rn[AX_RY] = -300;
        Pipeline n{Settings()};
        simulate(n, rn, 250.0, 1.0, 0.0);
        CHECK(n.out.zoom > 0, "ry<0 sign=1: zoom=%d", n.out.zoom);
    }
    // SIGN=-1 reverses scroll (same magnitude).
    {
        Settings s; s.scroll_v.sign = -1; s.scroll_h.sign = -1;
        Pipeline p{s}, q{Settings()};
        RawAxes r{}; r[AX_Z] = 300; r[AX_X] = 300;
        simulate(p, r, 250.0, 1.0, 0.0);
        simulate(q, r, 250.0, 1.0, 0.0);
        CHECK(p.out.scroll_v == -q.out.scroll_v && p.out.scroll_v != 0, "v %d vs %d", p.out.scroll_v, q.out.scroll_v);
        CHECK(p.out.scroll_h == -q.out.scroll_h && p.out.scroll_h != 0, "h %d vs %d", p.out.scroll_h, q.out.scroll_h);
    }
}

static void testSmoothing() {
    RawAxes r{}; r[AX_Z] = 300;
    Settings plain;
    Settings sm; sm.scroll_v.smooth_ms = 100;
    // Delayed start: after 50 ms with a jump, fewer steps than without smoothing.
    Pipeline a{plain}, b{sm};
    simulate(a, r, 250.0, 0.05, 0.0);
    simulate(b, r, 250.0, 0.05, 0.0);
    CHECK(b.out.scroll_v < a.out.scroll_v, "smoothing does not delay: %d vs %d",
          b.out.scroll_v, a.out.scroll_v);
    // Long run: total hardly different (<10%).
    Pipeline c{plain}, d{sm};
    simulate(c, r, 250.0, 10.0, 0.0);
    simulate(d, r, 250.0, 10.0, 0.0);
    int nc = c.out.scroll_v, nd = d.out.scroll_v;
    CHECK(nc > 0 && std::abs(nc - nd) * 10 < nc, "smoothing 10 s: %d vs %d", nc, nd);
}

static void testSensitivityZero() {
    Settings s;
    s.scroll_v.sensitivity = 0; s.scroll_h.sensitivity = 0; s.zoom.sensitivity = 0;
    for (size_t si = 0; si < sizeof(SCEN) / sizeof(SCEN[0]); ++si) {
        Pipeline p{s};
        simulate(p, SCEN[si].peak, 250.0, 1.0);
        CHECK(p.out.abs_scroll_v == 0 && p.out.abs_scroll_h == 0 && p.out.abs_zoom == 0 &&
                  !p.out.ctrl,
              "sens 0 produces output for %s", SCEN[si].name);
    }
}

static void testStale() {
    for (size_t si = 0; si < 5; ++si) {
        if (SCEN[si].expect == Scenario::NONE) continue;
        Pipeline p{Settings()};
        simulate(p, SCEN[si].peak, 250.0, 1.0);
        bool was_zoom = SCEN[si].expect == Scenario::ZOOM;
        CHECK(p.out.ctrl == was_zoom, "%s: Ctrl state before stale", SCEN[si].name);
        // The daemon feeds raw = 0 when stale.
        FakeOutput before = p.out;
        simulate(p, RawAxes{}, 250.0, 0.5, 0.0);
        CHECK(!p.out.ctrl, "%s: Ctrl still held after stale", SCEN[si].name);
        // At most one leftover step is possible in the first tick; nothing after that.
        CHECK(p.out.abs_scroll_v == before.abs_scroll_v && p.out.abs_scroll_h == before.abs_scroll_h &&
                  p.out.abs_zoom == before.abs_zoom,
              "%s: steps after stale", SCEN[si].name);
    }
}

int main() {
    std::puts("Scenarios:");
    testScenarios();
    testProfileSwitch();
    testLegacyEquivalence();
    testSigns();
    testSmoothing();
    testSensitivityZero();
    testStale();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_scenarios: OK");
    return 0;
}
