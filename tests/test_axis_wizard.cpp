// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/axis_wizard.hpp ("learn axes" wizard in the tray).
// Based on real peak values of a SpaceMouse Pro (x y z | rx ry rz):
//   Twist                 82 144  90 |  35 350 111
//   Tilt forward/back     61 125 140 | 350  50  80
//   Tilt sideways        107  95  65 | 107  62 350
//   Push sideways        350 293  60 | 132  70  75
//   Push forward/back     69 286 350 | 145  56 155
//   Lift                 170 350 149 |  59 103  81
// From these, synthetic sample sequences are built: ramp in the announced
// direction up to the peak, back to 0, then (weaker) in the opposite
// direction and back again - like a real back-and-forth.
#include <cassert>
#include <cstdio>
#include <string>

#include "axis_convention.hpp"
#include "axis_wizard.hpp"
#include "config.hpp"

using namespace spn;
using namespace spn::wiz;

// peaks: peak magnitude per axis; dir: sign of each axis in the FIRST
// half-movement (announced direction); back: factor for the return movement.
static std::vector<RawAxes> sweep(const RawAxes &peaks, const RawAxes &dir, double back = 0.8,
                                  int n = 40) {
    std::vector<RawAxes> s;
    auto add = [&](double f) {
        RawAxes v{};
        for (int a = 0; a < AX_COUNT; ++a) v[a] = int(peaks[a] * dir[a] * f);
        s.push_back(v);
    };
    s.push_back(RawAxes{});  // rest
    for (int i = 1; i <= n; ++i) add(double(i) / n);         // out
    for (int i = n - 1; i >= 0; --i) add(double(i) / n);     // back to 0
    for (int i = 1; i <= n; ++i) add(-back * i / n);         // opposite direction
    for (int i = n - 1; i >= 0; --i) add(-back * i / n);
    return s;
}

static const RawAxes P_TWIST = {82, 144, 90, 35, 350, 111};
static const RawAxes P_TILT_V = {61, 125, 140, 350, 50, 80};
static const RawAxes P_TILT_H = {107, 95, 65, 107, 62, 350};
static const RawAxes P_PUSH_H = {350, 293, 60, 132, 70, 75};
static const RawAxes P_PUSH_V = {69, 286, 350, 145, 56, 155};
static const RawAxes P_LIFT = {170, 350, 149, 59, 103, 81};

// Crosstalk signs deliberately mixed so that only the main axis counts.
static const RawAxes MIXED = {1, -1, 1, -1, 1, -1};

static RawAxes withMain(int axis, int sign) {
    RawAxes d = MIXED;
    d[axis] = sign;
    return d;
}

static void testPeaks() {
    auto p = peakAbs(sweep(P_PUSH_H, MIXED));
    for (int a = 0; a < AX_COUNT; ++a) assert(p[a] == P_PUSH_H[a]);
    assert(strongestAxis(p) == AX_X);
    assert(strongestAxis(p, AX_X) == AX_Y);
    assert(strongestAxis(RawAxes{}) == -1);
}

static void testSingleSteps() {
    struct Case { RawAxes peaks; int axis; };
    const Case cases[] = {{P_PUSH_V, AX_Z}, {P_TILT_V, AX_RX}, {P_PUSH_H, AX_X},
                          {P_TILT_H, AX_RZ}, {P_TWIST, AX_RY}, {P_LIFT, AX_Y}};
    for (const auto &c : cases)
        for (int sign : {1, -1}) {
            StepResult r = analyzeStep(sweep(c.peaks, withMain(c.axis, sign)));
            assert(r.valid && !r.too_weak);
            assert(r.axis == c.axis);
            assert(r.sign == sign);
            assert(r.peak >= 340 && r.peak <= 350);  // smoothed (median over 3 samples)
        }
    // Push sideways: y (293) shows strong crosstalk, x must still win.
    StepResult r = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, 1)));
    assert(r.runner_up == AX_Y && r.ratio > 0.8 && r.ratio < 0.85);
}

static void testSignFromFirstHalf() {
    // First half-movement weak (+60), return movement strong (-350):
    // the sign comes from the first movement above 40, not from the peak.
    std::vector<RawAxes> s;
    for (int v : {0, 20, 45, 60, 30, 0, -100, -250, -350, -200, 0}) {
        RawAxes a{};
        a[AX_RY] = v;
        a[AX_X] = v / 4;
        s.push_back(a);
    }
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_RY && r.sign == 1);
    // Noise below the threshold must not determine the sign.
    std::vector<RawAxes> n = {RawAxes{0, 0, -30, 0, 0, 0}, RawAxes{0, 0, 120, 0, 0, 0},
                              RawAxes{0, 0, 300, 0, 0, 0}};
    assert(analyzeStep(n).sign == 1);
}

static void testTooWeak() {
    RawAxes weak = {20, 30, 70, 25, 10, 30};  // Spitze 70 < 80
    StepResult r = analyzeStep(sweep(weak, withMain(AX_Z, 1)));
    assert(!r.valid && r.too_weak && r.axis == AX_Z && r.peak >= 67 && r.peak <= 70);
    assert(r.message.find("weak") != std::string::npos);
    StepResult e = analyzeStep({});
    assert(!e.valid && e.too_weak && e.axis == -1);
    StepResult z = analyzeStep({RawAxes{}, RawAxes{}});
    assert(!z.valid && z.too_weak);
}

static void testBuildMix() {
    assert(buildMix({{AX_Z, 1}, {AX_RX, 1}}) == "z + rx");
    assert(buildMix({{AX_X, 1}, {AX_RZ, -1}}) == "x - rz");
    assert(buildMix({{AX_RY, -1}}) == "-ry");
    assert(buildMix({{AX_Z, 1}, {-1, 0}}) == "z");
    assert(buildMix({{AX_RX, 1}, {AX_RX, -1}}) == "rx");  // duplicate axis only once
    assert(buildMix({}).empty());
    // The result must be accepted by the real parser and round-trip.
    for (const char *t : {"z + rx", "x - rz", "-ry", "-z - rx"}) {
        Mix m;
        std::string err;
        assert(parseMix(t, &m, &err));
        assert(mixToString(m) == t);
    }
}

// Full run with the real measured values and libspnav-typical signs
// (like the defaults "z + rx", "x - rz", "ry"): toward you = +z, tilt toward you = +rx,
// right = +x, tilt right = -rz, clockwise = +ry.
static std::array<StepResult, ROLE_COUNT> fullRun(int sz, int srx, int sx, int srz, int sry) {
    std::array<StepResult, ROLE_COUNT> res;
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_PUSH_V, withMain(AX_Z, sz)));
    res[ROLE_TILT_V] = analyzeStep(sweep(P_TILT_V, withMain(AX_RX, srx)));
    res[ROLE_PUSH_H] = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, sx)));
    res[ROLE_TILT_H] = analyzeStep(sweep(P_TILT_H, withMain(AX_RZ, srz)));
    res[ROLE_TWIST] = analyzeStep(sweep(P_TWIST, withMain(AX_RY, sry)));
    return res;
}

static void testEvaluate() {
    std::array<bool, ROLE_COUNT> none{};
    WizardResult w = evaluate(fullRun(1, 1, 1, -1, 1), none);
    assert(w.ok && w.errors.empty() && w.warnings.empty());
    assert(w.mix_v == "z + rx");
    assert(w.mix_h == "x - rz");
    assert(w.mix_zoom == "ry");

    // Different device / different mounting orientation: all signs flipped.
    w = evaluate(fullRun(-1, -1, -1, 1, -1), none);
    assert(w.ok && w.mix_v == "-z - rx" && w.mix_h == "-x + rz" && w.mix_zoom == "-ry");

    // Tilt steps skipped -> only one term each.
    std::array<bool, ROLE_COUNT> skip{};
    skip[ROLE_TILT_V] = skip[ROLE_TILT_H] = true;
    w = evaluate(fullRun(1, 1, 1, -1, 1), skip);
    assert(w.ok && w.mix_v == "z" && w.mix_h == "x" && w.mix_zoom == "ry");

    // Required step skipped -> cannot be applied.
    std::array<bool, ROLE_COUNT> skipReq{};
    skipReq[ROLE_TWIST] = true;
    w = evaluate(fullRun(1, 1, 1, -1, 1), skipReq);
    assert(!w.ok && !w.errors.empty());

    // Required step too weak -> error.
    auto res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_H] = analyzeStep(sweep(RawAxes{60, 40, 10, 20, 10, 15}, withMain(AX_X, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors.size() == 1);
    assert(w.errors[0].find("weak") != std::string::npos);

    // Contradiction: during "Push forward/back" the cap was actually tilted (rx wins)
    // -> same axis in two roles -> warning, rx only once in the mix.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_TILT_V, withMain(AX_RX, 1)));
    w = evaluate(res, none);
    assert(w.ok && w.warnings.size() == 1);
    assert(w.warnings[0].find("rx") != std::string::npos);
    assert(w.mix_v == "rx");

    // Axis duplicated across channels (twist detected as rz) -> error, cannot be applied.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_TWIST] = analyzeStep(sweep(P_TILT_H, withMain(AX_RZ, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors.size() == 1);
    assert(w.errors[0].find("rz") != std::string::npos);

    // Push forward/back and sideways both x -> error (not scroll_v=x, scroll_h=x).
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, 1)));
    w = evaluate(res, none);
    assert(!w.ok && !w.errors.empty());
    assert(w.errors[0].find("x") != std::string::npos);

    // Ambiguous step -> required step invalid.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_TWIST] = analyzeStep(sweep(RawAxes{0, 0, 0, 0, 300, 300}, withMain(AX_RY, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors[0].find("mbiguous") != std::string::npos);

    // y (lift/press) as strongest axis -> hint.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_LIFT, withMain(AX_Y, 1)));
    w = evaluate(res, none);
    bool hasY = false;
    for (const auto &m : w.warnings) hasY = hasY || m.find("lift/press") != std::string::npos;
    assert(hasY && w.mix_v == "y + rx");
    // Windows driver convention: y is the vertical push (no warning), z is lift/press (warning, no axis name).
    const Axis winLift = conventionLiftAxis(conventionFor("windows", "driver"));
    assert(winLift == AX_Z);
    w = evaluate(res, none, winLift);
    for (const auto &m : w.warnings) assert(m.find("lift/press") == std::string::npos);
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_PUSH_V, withMain(AX_Z, 1)));
    w = evaluate(res, none, winLift);
    hasY = false;
    for (const auto &m : w.warnings) {
        hasY = hasY || m.find("lift/press") != std::string::npos;
        assert(m.find("(z)") == std::string::npos && m.find("(y)") == std::string::npos);
    }
    assert(hasY);
    res = fullRun(1, 1, 1, -1, 1);

    // parseMix must accept all generated mixes.
    w = evaluate(fullRun(1, 1, 1, -1, 1), none);
    for (const std::string *t : {&w.mix_v, &w.mix_h, &w.mix_zoom}) {
        Mix m;
        std::string err;
        assert(parseMix(*t, &m, &err));
    }
}

// Residual motion from the previous step: a single sample in the opposite direction
// above the threshold must not flip the sign.
static void testSignRobust() {
    std::vector<RawAxes> s;
    for (int v : {-90, 0, 0, 50, 120, 250, 350, 200, 0, -100, 0}) {
        RawAxes a{};
        a[AX_RX] = v;
        s.push_back(a);
    }
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_RX && r.sign == 1);
    // Two separate twitches (-80, -70) with a pause in between: also no run.
    std::vector<RawAxes> t;
    for (int v : {-80, 0, -70, 0, 0, 60, 150, 300, 350, 200, 0}) {
        RawAxes a{};
        a[AX_RX] = v;
        t.push_back(a);
    }
    assert(analyzeStep(t).sign == 1);
    // Without a sustained excursion above the threshold: sign around the peak.
    std::vector<RawAxes> f;
    for (int v : {0, -30, -35, -38, -30, 0})  // alles <= 40
        f.push_back(RawAxes{0, 0, 0, 0, v, 0});
    assert(firstExcursionSign(f, AX_RY, 40, 2) == -1);
}

// A single spike on a crosstalk axis does not win.
static void testSpikeIgnored() {
    auto s = sweep(P_PUSH_V, withMain(AX_Z, 1));
    RawAxes spike{};
    spike[AX_RX] = 340;
    s.insert(s.begin() + 10, spike);
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_Z && r.sign == 1);
    assert(r.peaks[AX_RX] < 200);
    // Only spikes, never sustained -> no valid detection.
    std::vector<RawAxes> only;
    for (int i = 0; i < 10; ++i) only.push_back(RawAxes{});
    only[4][AX_X] = 300;
    only[8][AX_RZ] = 300;
    StepResult o = analyzeStep(only);
    assert(!o.valid && o.axis == -1);
}

// Nearly equally strong axes (also an exact tie) are ambiguous.
static void testAmbiguous() {
    RawAxes tie = {0, 0, 0, 0, 300, 300};
    StepResult r = analyzeStep(sweep(tie, withMain(AX_RY, 1)));
    assert(!r.valid && r.ambiguous && !r.too_weak);
    RawAxes near = {0, 0, 0, 0, 300, 290};  // ratio 0.967
    r = analyzeStep(sweep(near, withMain(AX_RY, 1)));
    assert(!r.valid && r.ambiguous);
    RawAxes ok = {0, 0, 0, 0, 300, 250};  // 0.83: valid
    r = analyzeStep(sweep(ok, withMain(AX_RY, 1)));
    assert(r.valid && !r.ambiguous && r.axis == AX_RY);
}

static const char *fakeTr(const char *s) {
    return std::string(s).compare(0, 8, "Too weak") == 0 ? "Zu schwach (Spitze %d auf %s, noetig %d)" : s;
}

// The tray installs its translator via the hook; messages use the translated format.
static void testTranslationHook() {
    RawAxes weak = {20, 30, 70, 25, 10, 30};
    StepResult en = analyzeStep(sweep(weak, withMain(AX_Z, 1)));
    assert(en.message.rfind("Too weak (peak ", 0) == 0);
    auto *old = trHook();
    trHook() = fakeTr;
    StepResult de = analyzeStep(sweep(weak, withMain(AX_Z, 1)));
    trHook() = old;
    assert(de.message.rfind("Zu schwach (Spitze ", 0) == 0);
    assert(de.message.find(" auf z, noetig 80)") != std::string::npos);
}

static void testHalfMovements() {
    std::vector<RawAxes> s;
    auto push = [&](int v, int n) {
        for (int i = 0; i < n; i++) {
            RawAxes a{};
            a[AX_Z] = v;
            s.push_back(a);
        }
    };
    assert(countHalfMovements(s, AX_Z, 40) == 0);
    push(200, 5);
    assert(countHalfMovements(s, AX_Z, 40) == 1);
    push(0, 5);
    push(200, 5);  // the same way again is still one half-movement
    assert(countHalfMovements(s, AX_Z, 40) == 1);
    push(-200, 5);
    assert(countHalfMovements(s, AX_Z, 40) == 2);
    assert(countHalfMovements(s, AX_X, 40) == 0 && countHalfMovements(s, -1, 40) == 0);
    std::vector<RawAxes> spike(1);
    spike[0][AX_Z] = 300;  // a single sample is no half-movement
    assert(countHalfMovements(spike, AX_Z, 40) == 0);
}

int main() {
    testTranslationHook();
    testSignRobust();
    testSpikeIgnored();
    testAmbiguous();
    testPeaks();
    testSingleSteps();
    testSignFromFirstHalf();
    testTooWeak();
    testBuildMix();
    testEvaluate();
    testHalfMovements();
    printf("test_axis_wizard: all tests passed\n");
    return 0;
}
