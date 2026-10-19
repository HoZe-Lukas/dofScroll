// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit-Tests fuer src/dsp.cpp (assert-basiert, eigenes main).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "dsp.hpp"

using namespace spn;

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static void testDeadzone() {
    assert(applyDeadzone(20, 30) == 0.0);
    assert(applyDeadzone(-30, 30) == 0.0);
    assert(applyDeadzone(50, 30) == 20.0);
    assert(applyDeadzone(-50, 30) == -20.0);
    assert(applyDeadzone(5, 0) == 5.0);
}

static void testCurve() {
    ChannelCfg c;
    for (double v : {-300.0, -1.0, 0.0, 7.5, 200.0}) assert(applyCurve(c, v) == v);
    c.curve_exp = 2.0;
    assert(near(applyCurve(c, 100), 50.0, 1e-9));   // (0.5)^2 * 200
    assert(near(applyCurve(c, -100), -50.0, 1e-9));
    assert(near(applyCurve(c, 200), 200.0, 1e-9));
    c.curve_points = {{0, 0}, {50, 0.1}, {200, 1}, {350, 3}};
    assert(applyCurve(c, 0) == 0.0);
    assert(near(applyCurve(c, 50), 5.0, 1e-9));
    assert(near(applyCurve(c, 200), 200.0, 1e-9));
    assert(near(applyCurve(c, 125), 125 * 0.55, 1e-9));  // Mitte 50..200
    assert(near(applyCurve(c, -125), -125 * 0.55, 1e-9));
    assert(near(applyCurve(c, 1000), 3000.0, 1e-9));      // geklemmt auf Faktor 3
    assert(near(applyCurve(c, 275), 275 * 2.0, 1e-9));
}

static double stepResponse(double hz, double tau_ms, double t_s) {
    Smoother s;
    int n = (int)std::lround(hz * t_s);
    double y = 0;
    for (int i = 0; i < n; ++i) y = s.step(1.0, 1.0 / hz, tau_ms);
    return y;
}

static void testSmoother() {
    Smoother p;
    assert(p.step(42.0, 0.008, 0.0) == 42.0);
    double a = stepResponse(125, 80, 0.08);
    assert(near(a, 1.0 - std::exp(-1.0), 1e-9));
    assert(near(a, 0.632, 0.01));
    assert(near(stepResponse(250, 80, 0.08), a, 1e-6));
    assert(near(stepResponse(1000, 80, 0.32), stepResponse(125, 80, 0.32), 1e-6));
}

static void testChannel() {
    Settings s;
    ChannelCfg &c = s.scroll_v;
    c.mix.terms = {{AX_Z, 1.0}};
    c.sensitivity = 5; c.deadzone = 0; c.sign = 1; c.smooth_ms = 0;
    RawAxes raw{};
    raw[AX_Z] = 2500;
    Channel ch;
    assert(near(ch.process(c, raw, 0.008), 2500.0 * 5 / 2500.0 * 125.0, 1e-9));
    c.sign = -1;
    assert(near(ch.process(c, raw, 0.008), -625.0, 1e-9));
    c.sensitivity = 0;
    assert(ch.process(c, raw, 0.008) == 0.0);
    c.sensitivity = 5; c.sign = 1; c.deadzone = 500;
    assert(near(ch.process(c, raw, 0.008), 2000.0 * 5 / 2500.0 * 125.0, 1e-9));
}

static void testAccumulator() {
    const double rate = 37.3;
    for (double hz : {30.0, 125.0, 250.0, 1000.0}) {
        StepAccumulator a;
        int sum = 0, n = (int)hz;
        for (int i = 0; i < n; ++i) sum += a.add(rate, 1.0 / hz);
        assert(std::abs(sum - rate) <= 1.0);
        assert(sum >= 36 && sum <= 38);
    }
    StepAccumulator a;  // negativ + Vorzeichenwechsel
    int sum = 0;
    for (int i = 0; i < 250; ++i) sum += a.add(-20.0, 0.004);
    assert(sum == -20 || sum == -19);  // Rundungsrest
    assert(a.add(0.0, 0.004) == 0);
    assert(a.add(1000.0, 0.0005) == 0);   // 0.5 Rest, kein Schritt
    assert(a.add(-1000.0, 0.0015) == -1); // 0.5 - 1.5 = -1.0
    assert(a.add(0.0, 0.004) == 0);
}

static void testZoom() {
    Settings s;  // dz 40, enter 40ms, exit_ratio 1.5
    s.zoom_enter_ms = 40;
    const double dt = 0.004;
    {   // reiner Twist: nicht frueher als enter_ms
        ZoomDetector z;
        int tick = 0;
        while (!z.update(100, 0, s, dt)) { ++tick; assert(tick < 100); }
        assert((tick + 1) * dt * 1000.0 >= 40 - 1e-6);
        assert(tick + 1 == 10);
        assert(z.active());
    }
    {   // Scroll mit Twist-Leck: twist < lateral -> nie
        ZoomDetector z;
        for (int i = 0; i < 1000; ++i) assert(!z.update(60, 200, s, dt));
    }
    {   // Unterbrechung setzt Kandidat zurueck
        ZoomDetector z;
        for (int i = 0; i < 9; ++i) assert(!z.update(100, 0, s, dt));
        assert(!z.update(10, 0, s, dt));
        for (int i = 0; i < 9; ++i) assert(!z.update(100, 0, s, dt));
        assert(z.update(100, 0, s, dt));
    }
    {   // Hysterese
        ZoomDetector z;
        for (int i = 0; i < 20; ++i) z.update(100, 0, s, dt);
        assert(z.active());
        assert(z.update(100, 140, s, dt));    // 140 <= 150 -> bleibt
        assert(!z.update(100, 160, s, dt));   // > 150 -> aus
        z.reset();
        for (int i = 0; i < 20; ++i) z.update(100, 0, s, dt);
        assert(z.update(41, 0, s, dt));
        assert(!z.update(40, 0, s, dt));      // <= Deadzone -> aus
    }
    {   // Messwerte: Drehen
        RawAxes raw{};
        raw[AX_X] = 82; raw[AX_Y] = 144; raw[AX_Z] = 90; raw[AX_RX] = 35;
        raw[AX_RY] = 350; raw[AX_RZ] = 111;
        double lat = lateralMax(s.zoom_lateral, raw);
        assert(lat == 111.0);  // y zaehlt nicht zu x z rx rz
        double tw = std::fabs(evalMix(s.zoom.mix, raw) * s.zoom.sign);
        assert(tw == 350.0);
        ZoomDetector z; bool on = false;
        for (int i = 0; i < 20; ++i) on = z.update(tw, lat, s, dt);
        assert(on);
    }
    {   // Messwerte: seitlich schieben
        RawAxes raw{};
        raw[AX_X] = 350; raw[AX_RY] = 70;
        double lat = lateralMax(s.zoom_lateral, raw);
        double tw = std::fabs(evalMix(s.zoom.mix, raw) * s.zoom.sign);
        ZoomDetector z;
        for (int i = 0; i < 500; ++i) assert(!z.update(tw, lat, s, dt));
    }
}

// Regression S2 (docs/security.md): extreme Config-Werte duerfen weder inf/NaN
// noch eine Klickflut erzeugen.
static void testExtremeValuesBounded() {
    Settings s;
    ChannelCfg c = s.scroll_v;
    c.curve_exp = 60.0;  // ueber der Config-Grenze: DSP muss trotzdem begrenzen
    c.sensitivity = 255;
    Channel ch;
    double rate = ch.processValue(c, 350.0, 0.004);
    assert(std::isfinite(rate) && std::fabs(rate) <= 20000.0);

    c.curve_exp = 1.0;
    c.curve_points = {{0, 0}, {350, 1e6}};  // Faktor weit ueber der Config-Grenze
    rate = Channel().processValue(c, 350.0, 0.004);
    assert(std::isfinite(rate) && std::fabs(rate) <= 20000.0);

    StepAccumulator acc;
    assert(std::abs(acc.add(1e18, 0.004)) <= 200);
    assert(acc.add(std::nan(""), 0.004) == 0);
    assert(acc.add(std::numeric_limits<double>::infinity(), 0.004) == 0);
}

int main() {
    testExtremeValuesBounded();
    testDeadzone();
    testCurve();
    testSmoother();
    testChannel();
    testAccumulator();
    testZoom();
    std::puts("test_dsp: alle Tests bestanden");
    return 0;
}
