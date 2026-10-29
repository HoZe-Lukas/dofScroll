// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The pointer as a motion function (stage 5 of #224, src/pointer_engine.hpp): `pointer_x` and `pointer_y` on every motion of
// a profile or a joystick mode, the global MOUSE_SPEED (pixels per step), the per-call cap, and that nothing is left over after
// a release. A fake output records every Output::moveMouse call. The unchanged output of all old configurations is pinned by
// tests/test_golden_motions.cpp and tests/test_mode_compat.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "pointer_engine.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct PtrRec : Output {
    struct Move { int dx, dy; };
    std::vector<Move> moves;
    int wv = 0, wh = 0, other = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { wv += n; }
    void wheelH(int n) override { wh += n; }
    void setKey(Key, bool) override { ++other; }
    void tapKey(Key) override { ++other; }
    void mouseButton(int, bool) override { ++other; }
    void moveMouse(int dx, int dy) override { moves.push_back({dx, dy}); }
    void releaseAll() override {}
    void flush() override {}
    long sumX() const { long t = 0; for (const Move &m : moves) t += m.dx; return t; }
    long sumY() const { long t = 0; for (const Move &m : moves) t += m.dy; return t; }
    int maxAbs() const {
        int t = 0;
        for (const Move &m : moves) t = std::max(t, std::max(std::abs(m.dx), std::abs(m.dy)));
        return t;
    }
};

static const double DT = 1.0 / 250.0;

static Settings conf(const std::vector<KV> &kv, int layer = kLayerGlobal) {
    Settings s;
    for (const KV &p : kv) {
        std::string err;
        const bool ok = applyKeyLayer(s, p.first, p.second, layer, &err);
        if (!ok) std::fprintf(stderr, "bad key %s = %s: %s\n", p.first.c_str(), p.second.c_str(), err.c_str());
        assert(ok);
    }
    return s;
}

static RawAxes only(Axis a, int v) {
    RawAxes r{};
    r[size_t(a)] = v;
    return r;
}

static void run(ModeRunner &r, const Settings &s, const RawAxes &raw, double secs, double dt = DT) {
    for (int i = 0; i < static_cast<int>(secs / dt + 0.5); ++i) r.tick(s, raw, 0, true, dt);
}

// ---------------------------------------------------------------- parser and settings
static void testParser() {
    ChannelAction a;
    std::string err;
    CHECK(parseChannelAction("pointer_x", &a, &err) && a.type == ChannelActionType::PointerX, "pointer_x");
    CHECK(parseChannelAction("  Pointer_Y ", &a, &err) && a.type == ChannelActionType::PointerY, "case and blanks");
    CHECK(channelActionText(a) == "pointer_y", "text round trip");
    CHECK(!a.usesKeys(), "the pointer uses no keys");
    ChannelAction b;
    CHECK(parseChannelAction(channelActionText(a), &b, &err) && b == a, "round trip equal");
    CHECK(!(ChannelAction() == a), "different from default");
    ChannelAction x;
    x.type = ChannelActionType::PointerX;
    CHECK(!(x == a), "pointer_x differs from pointer_y");
    for (const char *bad : {"pointer", "pointer_z", "pointer_x:1", "pointer_x,pointer_y", "pointer_x pointer_y", "pointerx", "pointer_",
                            "mouse_x", "pointer_x,none", "keys:pointer_x,a"}) {
        err.clear();
        CHECK(!parseChannelAction(bad, &a, &err) && !err.empty(), "rejected: %s", bad);
    }
    // the lift is continuous: the pointer is allowed on it
    CHECK(parseLiftAction("pointer_y", &a, &err) && a.type == ChannelActionType::PointerY, "lift accepts pointer_y");
}

static void testSettingsKeys() {
    Settings d;
    CHECK(d.mouse_speed == 8.0 && kDefaultMouseSpeed == 8.0, "MOUSE_SPEED default 8");
    std::string err;
    Settings s;
    CHECK(applyKey(s, "MOUSE_SPEED", "12.5", &err) && s.mouse_speed == 12.5, "12.5");
    CHECK(applyKey(s, "MOUSE_SPEED", std::to_string(kMinMouseSpeed), &err), "lower end");
    CHECK(applyKey(s, "MOUSE_SPEED", std::to_string(kMaxMouseSpeed), &err), "upper end");
    for (const char *bad : {"0", "-1", "abc", "", "nan", "inf", "1e9", "0.00001", "201", "8 px"}) {
        Settings t;
        err.clear();
        CHECK(!applyKey(t, "MOUSE_SPEED", bad, &err) && !err.empty() && t.mouse_speed == 8.0, "MOUSE_SPEED rejects '%s'", bad);
    }
    // all action keys of a profile take it, the same validation as every action
    for (const char *key : {"V_ACTION", "H_ACTION", "TWIST_ACTION", "LIFT_ACTION", "V_PUSH_ACTION", "V_TILT_ACTION", "H_PUSH_ACTION", "H_TILT_ACTION"}) {
        Settings t;
        CHECK(applyKeyLayer(t, key, "pointer_x", kLayerGlobal, &err), "%s accepts pointer_x", key);
        Settings u;
        CHECK(!applyKeyLayer(u, key, "pointer_q", kLayerGlobal, &err), "%s rejects pointer_q", key);
    }
    // the written configuration keeps MOUSE_SPEED (round trip through the key list)
    Settings w = conf({{"MOUSE_SPEED", "3"}});
    bool found = false;
    for (const KV &kv : allKeys(w))
        if (kv.first == "MOUSE_SPEED") found = kv.second == "3";
    CHECK(found, "MOUSE_SPEED is written");
    // profile text: a section with pointer actions loads without a warning, an invalid value is skipped with one
    ProfileSet ps;
    std::vector<std::string> warn;
    parseProfilesText("[app:game]\nH_PUSH_ACTION = pointer_x\nV_PUSH_ACTION = pointer_y\nMOUSE_SPEED = 5\nV_TILT_ACTION = pointer_zz\n", "t", &ps, &warn);
    CHECK(warn.size() == 1 && warn[0].find("V_TILT_ACTION") != std::string::npos, "one warning for the bad value");
    const Settings r = ps.resolve("", "game");
    CHECK(r.mouse_speed == 5.0 && r.actions[kLayerApp].part[PART_H_PUSH].type == ChannelActionType::PointerX &&
              r.actions[kLayerApp].part[PART_V_PUSH].type == ChannelActionType::PointerY && !r.actions[kLayerApp].partSet[PART_V_TILT],
          "resolved profile");
}

static void testModeFile() {
    const std::string dir = tu::makeTempDir("ptrmode");
    {
        std::ofstream f(dir + "/aim.conf", std::ios::binary);
        f << "NAME = Aim\nDESCRIPTION = x\nH_ACTION = pointer_x\nV_PUSH_ACTION = pointer_y\nV_TILT_ACTION = none\nTWIST_ACTION = pointer_x\n"
             "LIFT_ACTION = pointer_y\nMOUSE_SPEED = 4\n";
    }
    {
        std::ofstream f(dir + "/bad.conf", std::ios::binary);
        f << "NAME = Bad\nH_ACTION = pointer_w\nLIFT_ACTION = pointer_y\n";
    }
    std::vector<std::string> warn;
    const ModeSet ms = loadModes(dir, &warn);
    const ModeDef *m = ms.find("aim");
    CHECK(m && m->h.type == ChannelActionType::PointerX && m->vSplit.push.type == ChannelActionType::PointerY &&
              m->twist.type == ChannelActionType::PointerX && m->lift.type == ChannelActionType::PointerY,
          "mode file with pointer actions");
    const ModeDef *bad = ms.find("bad");
    CHECK(bad && bad->h.type == ChannelActionType::Default, "invalid pointer value of a mode file is ignored (line skipped)");
    CHECK(!warn.empty(), "a warning was given for the bad line");
    // the mode drives the runner: sideways push moves right
    if (m) {
        PtrRec o;
        ModeRunner r(&o);
        r.setMode(*m);
        run(r, Settings(), only(AX_X, 300), 0.5);
        CHECK(o.sumX() > 0 && o.sumY() == 0, "mode: H_ACTION pointer_x moves right");
    }
    tu::removeAll(dir);
}

// ---------------------------------------------------------------- engine
static void testEngineUnits() {
    PointerMover p;
    CHECK(p.step(0.0, DT, 8.0) == 0, "rest");
    // 10 steps/s * 8 px = 80 px/s = 0.32 px per tick: first whole pixel in tick 4
    int first = -1;
    for (int i = 0; i < 20 && first < 0; ++i)
        if (p.step(10.0, DT, 8.0) == 1) first = i;
    CHECK(first == 3, "first pixel after the fractions add up, got tick %d", first);
    // the remainder is dropped at rest
    p.reset();
    for (int i = 0; i < 2; ++i) p.step(10.0, DT, 8.0);
    CHECK(p.step(0.0, DT, 8.0) == 0, "rest returns 0");
    int again = -1;
    for (int i = 0; i < 20 && again < 0; ++i)
        if (p.step(10.0, DT, 8.0) == 1) again = i;
    CHECK(again == 3, "no residue after rest, got tick %d", again);
    // signs
    p.reset();
    long sum = 0;
    for (int i = 0; i < 250; ++i) sum += p.step(-10.0, DT, 8.0);
    CHECK(sum <= -79 && sum >= -80, "negative side: about -80 px in a second, got %ld", sum);
    // non-finite values never move
    p.reset();
    CHECK(p.step(std::nan(""), DT, 8.0) == 0 && p.step(INFINITY, DT, 8.0) == 0 && p.step(1.0, DT, std::nan("")) == 0, "non-finite");
    // the cap per call, both signs, also with an absurd dt
    p.reset();
    CHECK(p.step(1e6, 1.0, 200.0) == kMaxMouseStep && p.step(-1e6, 1.0, 200.0) == -kMaxMouseStep, "capped at kMaxMouseStep per call");
    p.reset();
    CHECK(p.step(100.0, 1.0, 200.0) == kMaxMouseStep, "20000 px in one call is capped");
    CHECK(p.step(0.0, 1.0, 200.0) == 0, "the capped excess is dropped, not carried");
}

// ---------------------------------------------------------------- runner

static void testSumsAndSigns() {
    // Reference: the same deflection as a wheel gives n steps; the pointer gives MOUSE_SPEED pixels per step.
    for (int axisCase = 0; axisCase < 2; ++axisCase) {
        const Axis a = axisCase == 0 ? AX_Z : AX_X;  // default: z = V push, x = H push
        PtrRec w;
        ModeRunner rw(&w);
        run(rw, conf({{axisCase == 0 ? "V_ACTION" : "H_ACTION", axisCase == 0 ? "wheel_v" : "wheel_h"}}), only(a, 300), 1.0);
        const long steps = axisCase == 0 ? w.wv : w.wh;
        assert(steps > 10);
        PtrRec o;
        ModeRunner r(&o);
        run(r, conf({{axisCase == 0 ? "V_ACTION" : "H_ACTION", axisCase == 0 ? "pointer_y" : "pointer_x"}, {"MOUSE_SPEED", "8"}}), only(a, 300), 1.0);
        const long px = axisCase == 0 ? o.sumY() : o.sumX();
        const long other = axisCase == 0 ? o.sumX() : o.sumY();
        CHECK(std::labs(px - 8 * steps) <= 8 + 2, "axis %d: %ld px for %ld steps", axisCase, px, steps);
        CHECK(other == 0 && o.wv == 0 && o.wh == 0 && o.other == 0, "only the named pointer axis moves, no wheel, no key");
        // negative direction, the other sign
        PtrRec n;
        ModeRunner rn(&n);
        run(rn, conf({{axisCase == 0 ? "V_ACTION" : "H_ACTION", axisCase == 0 ? "pointer_y" : "pointer_x"}}), only(a, -300), 1.0);
        const long npx = axisCase == 0 ? n.sumY() : n.sumX();
        CHECK(std::labs(npx + px) <= 2, "symmetric: %ld vs %ld", npx, px);
    }
    // MOUSE_SPEED scales linearly
    PtrRec o4, o16;
    ModeRunner r4(&o4), r16(&o16);
    run(r4, conf({{"V_ACTION", "pointer_y"}, {"MOUSE_SPEED", "4"}}), only(AX_Z, 300), 1.0);
    run(r16, conf({{"V_ACTION", "pointer_y"}, {"MOUSE_SPEED", "16"}}), only(AX_Z, 300), 1.0);
    CHECK(std::labs(o16.sumY() - 4 * o4.sumY()) <= 8, "MOUSE_SPEED 16 is four times 4: %ld vs %ld", o16.sumY(), o4.sumY());
    // SIGN flips the direction like for the wheel
    PtrRec os;
    ModeRunner rs(&os);
    run(rs, conf({{"V_ACTION", "pointer_y"}, {"SCROLL_V_SIGN", "-1"}}), only(AX_Z, 300), 1.0);
    CHECK(os.sumY() < 0, "SCROLL_V_SIGN -1 moves up");
}

static void testEveryMotion() {
    struct Case { const char *key; Axis axis; int value; bool x; };
    const Case cases[] = {
        {"V_PUSH_ACTION", AX_Z, 300, false}, {"V_TILT_ACTION", AX_RX, 300, true},  {"H_PUSH_ACTION", AX_X, 300, true},
        {"H_TILT_ACTION", AX_RZ, 300, false}, {"TWIST_ACTION", AX_RY, 300, true},  {"LIFT_ACTION", AX_Y, 400, false},
    };
    for (const Case &c : cases) {
        PtrRec o;
        ModeRunner r(&o);
        const Settings s = conf({{c.key, c.x ? "pointer_x" : "pointer_y"}});
        run(r, s, only(c.axis, c.value), 1.0);
        const long on = c.x ? o.sumX() : o.sumY(), off = c.x ? o.sumY() : o.sumX();
        CHECK(std::labs(on) > 10, "%s: the pointer moves (%ld)", c.key, on);
        CHECK(off == 0, "%s: the other pointer axis stays (%ld)", c.key, off);
        CHECK(r.axesActive(), "%s: counts as active while deflected", c.key);
        run(r, s, RawAxes{}, 0.5);
        CHECK(!r.axesActive(), "%s: at rest again", c.key);
    }
}

static void testProfileLayers() {
    // [app:*] beats the mode and [global]; a program without pointer actions keeps the old output
    PtrRec o;
    ModeRunner r(&o);
    ProfileSet ps;
    parseProfilesText("[global]\nH_ACTION = wheel_h\n[app:game]\nH_ACTION = pointer_x\nMOUSE_SPEED = 2\n", "t", &ps, nullptr);
    run(r, ps.resolve("", ""), only(AX_X, 300), 0.5);
    CHECK(o.wh > 0 && o.moves.empty(), "global: wheel, no pointer");
    PtrRec g;
    ModeRunner rg(&g);
    run(rg, ps.resolve("", "game"), only(AX_X, 300), 0.5);
    CHECK(g.wh == 0 && g.sumX() > 0, "app: the pointer, not the wheel");
    PtrRec g2;
    ModeRunner rg2(&g2);
    Settings fast = ps.resolve("", "game");
    fast.mouse_speed = 20.0;
    run(rg2, fast, only(AX_X, 300), 0.5);
    CHECK(g2.sumX() > 5 * g.sumX(), "the speed of the resolved profile counts");
}

static void testCapAtRunner() {
    // a slow tick (30 Hz) with the maximum speed and a full deflection never moves more than kMaxMouseStep per call
    PtrRec o;
    ModeRunner r(&o);
    const Settings s = conf({{"V_ACTION", "pointer_y"}, {"MOUSE_SPEED", "200"}, {"SCROLL_V_SENS", "255"}, {"TICK_HZ", "30"}});
    run(r, s, only(AX_Z, 100000), 1.0, 1.0 / 30.0);
    CHECK(!o.moves.empty() && o.maxAbs() <= kMaxMouseStep, "per-call delta %d <= %d", o.maxAbs(), kMaxMouseStep);
    // big dt (a stalled tick): still one capped call, never a flood
    PtrRec p;
    ModeRunner rp(&p);
    run(rp, s, only(AX_Z, 100000), 5.0, 1.0);
    CHECK(p.moves.size() == 5 && p.maxAbs() <= kMaxMouseStep, "5 slow ticks give 5 calls of at most the cap");
    // a tick without movement makes no call at all
    PtrRec q;
    ModeRunner rq(&q);
    run(rq, s, RawAxes{}, 1.0);
    CHECK(q.moves.empty(), "no call at rest");
}

static void testSameFunctionOnTwoMotionsAdds() {
    PtrRec a, b, both;
    ModeRunner ra(&a), rb(&b), rboth(&both);
    run(ra, conf({{"V_PUSH_ACTION", "pointer_y"}}), only(AX_Z, 300), 1.0);
    run(rb, conf({{"V_TILT_ACTION", "pointer_y"}}), only(AX_RX, 300), 1.0);
    RawAxes both_axes = only(AX_Z, 300);
    both_axes[AX_RX] = 300;
    run(rboth, conf({{"V_PUSH_ACTION", "pointer_y"}, {"V_TILT_ACTION", "pointer_y"}}), both_axes, 1.0);
    CHECK(std::labs(both.sumY() - (a.sumY() + b.sumY())) <= 3, "push and tilt add up: %ld vs %ld + %ld", both.sumY(), a.sumY(), b.sumY());
    // one call per tick (the sum of the motions), not one per motion
    CHECK(both.moves.size() <= 250, "one moveMouse call per tick, got %zu", both.moves.size());
    // x and y from two motions come in the same call
    PtrRec xy;
    ModeRunner rxy(&xy);
    RawAxes ax = only(AX_Z, 300);
    ax[AX_X] = 300;
    run(rxy, conf({{"V_PUSH_ACTION", "pointer_y"}, {"H_PUSH_ACTION", "pointer_x"}}), ax, 0.2);
    bool both_in_one = false;
    for (const PtrRec::Move &m : xy.moves) both_in_one = both_in_one || (m.dx != 0 && m.dy != 0);
    CHECK(both_in_one && xy.moves.size() <= 50, "dx and dy in one call");
}

static void testReleasePaths() {
    // A slow deflection leaves a fractional remainder. After every release path the next movement starts from zero, exactly
    // like on a fresh runner.
    const Settings s = conf({{"V_ACTION", "pointer_y"}});
    auto first = [&](ModeRunner &r, PtrRec &o) {
        o.moves.clear();
        for (int i = 0; i < 400; ++i) {
            r.tick(s, only(AX_Z, 120), 0, true, DT);
            if (!o.moves.empty()) return i;
        }
        return -1;
    };
    PtrRec fresh;
    ModeRunner rf(&fresh);
    const int base = first(rf, fresh);
    CHECK(base > 0, "baseline: the first pixel does not come in tick 0 (%d)", base);
    const int stop = std::max(1, base - 1);  // leaves a remainder, stops one tick before the first pixel
    enum Path { SetMode, ReleaseAll, ResetDsp, Rest, SuspendZero };
    for (int p = SetMode; p <= SuspendZero; ++p) {
        PtrRec o;
        ModeRunner r(&o);
        for (int i = 0; i < stop; ++i) r.tick(s, only(AX_Z, 120), 0, true, DT);
        CHECK(o.moves.empty(), "path %d: no pixel yet", p);
        switch (p) {
        case SetMode: { ModeDef m; m.id = "other"; r.setMode(m); break; }
        case ReleaseAll: r.releaseAll(); break;
        case ResetDsp: r.resetDsp(); break;
        case Rest: r.tick(s, RawAxes{}, 0, true, DT); break;
        case SuspendZero: r.tick(s, RawAxes{}, 0, false, DT); r.releaseAll(); break;
        }
        const int again = first(r, o);
        CHECK(again == base, "path %d: starts from zero (%d, fresh %d)", p, again, base);
    }
    // the smoothing tail of a released motion is finite: after ENABLED=0 style zero input nothing keeps moving
    PtrRec o;
    ModeRunner r(&o);
    run(r, conf({{"V_ACTION", "pointer_y"}, {"SCROLL_V_SMOOTH_MS", "100"}}), only(AX_Z, 400), 0.5);
    o.moves.clear();
    r.releaseAll();
    run(r, conf({{"V_ACTION", "pointer_y"}, {"SCROLL_V_SMOOTH_MS", "100"}}), RawAxes{}, 1.0);
    CHECK(o.moves.empty(), "after releaseAll a zero input moves nothing (no smoothing tail)");
    CHECK(!r.holdsAnything(), "the pointer holds nothing on the output");
}

// A pointer part and a macro part side by side (stage 5 with the macro core): V_PUSH moves the pointer, V_TILT runs a macro.
static void testPointerWithMacroPart() {
    PtrRec o;
    ModeRunner r(&o);
    const Settings s = conf({{"V_PUSH_ACTION", "pointer_y"}, {"V_TILT_ACTION", "macro:+a -a,+b -b"}});
    run(r, s, only(AX_Z, 300), 0.5);
    CHECK(o.sumY() != 0 && o.sumX() == 0, "V_PUSH moves the pointer while V_TILT holds a macro");
    r.releaseAll();
    o.moves.clear();
    r.tick(s, RawAxes{}, 0, true, DT);
    CHECK(o.moves.empty(), "nothing moves after releaseAll");
}

static void testOldConfigsUnchanged() {
    // Without a pointer action no moveMouse call is ever made, on any path.
    PtrRec o;
    ModeRunner r(&o);
    for (const char *act : {"wheel_v", "zoom", "keys:pageup,pagedown", "step:pageup,pagedown", "none", "default"}) {
        const Settings s = conf({{"V_ACTION", act}, {"H_ACTION", act}, {"TWIST_ACTION", "wheel_v"}});
        RawAxes raw = only(AX_Z, 300);
        raw[AX_X] = 300;
        raw[AX_RY] = 200;
        run(r, s, raw, 0.5);
    }
    run(r, Settings(), only(AX_Z, 300), 0.5);
    CHECK(o.moves.empty(), "no pointer movement without a pointer action");
    // MOUSE_SPEED alone changes nothing
    PtrRec a, b;
    ModeRunner ra(&a), rb(&b);
    run(ra, conf({{"V_ACTION", "keys:pageup,pagedown"}}), only(AX_Z, 300), 0.5);
    run(rb, conf({{"V_ACTION", "keys:pageup,pagedown"}, {"MOUSE_SPEED", "99"}}), only(AX_Z, 300), 0.5);
    CHECK(a.other == b.other && a.moves.empty() && b.moves.empty(), "MOUSE_SPEED does not touch the key output");
    CHECK(Settings().mouse_speed == 8.0, "default unchanged");
}

int main() {
    testParser();
    testSettingsKeys();
    testModeFile();
    testEngineUnits();
    testSumsAndSigns();
    testEveryMotion();
    testProfileLayers();
    testCapAtRunner();
    testSameFunctionOnTwoMotionsAdds();
    testReleasePaths();
    testPointerWithMacroPart();
    testOldConfigsUnchanged();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_pointer_motion: ok");
    return 0;
}
