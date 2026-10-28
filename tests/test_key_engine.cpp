// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The key engine (stage 3 of #224, src/key_engine.hpp): KeyRepeater (analog keys:) and DigitalTrigger (digital step:), the
// per-motion keys <P>KEY_RATE_MIN / <P>KEY_RATE_MAX / <P>TRIGGER in profiles and joystick modes, `none` per side and the tier
// rule (an analog keys: with a risky key is refused). The exact output of the default parameters is pinned by
// tests/test_golden_motions.cpp; the release paths are in tests/test_stuck_keys.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "dsp.hpp"
#include "key_engine.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct Rec : Output {
    std::vector<std::string> ev;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { ev.push_back("wv" + std::to_string(n)); }
    void wheelH(int n) override { ev.push_back("wh" + std::to_string(n)); }
    void setKey(Key k, bool d) override { ev.push_back(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { ev.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override { ev.push_back(std::string(d ? "+m" : "-m") + std::to_string(b)); }
    void releaseAll() override {}
    void flush() override {}
    int count(const std::string &e) const {
        int c = 0;
        for (const auto &x : ev) c += x == e;
        return c;
    }
    int taps() const {
        int c = 0;
        for (const auto &x : ev) c += x.compare(0, 4, "tap:") == 0;
        return c;
    }
};

static const double DT = 1.0 / 250.0;

static Settings conf(const std::vector<KV> &kv) {
    Settings s;
    for (const KV &p : kv) {
        std::string err;
        const bool ok = applyKeyLayer(s, p.first, p.second, kLayerGlobal, &err);
        if (!ok) std::fprintf(stderr, "bad key %s = %s: %s\n", p.first.c_str(), p.second.c_str(), err.c_str());
        assert(ok);
    }
    return s;
}

static RawAxes axes(int x, int y, int z, int rx, int ry, int rz) {
    RawAxes r{};
    r[AX_X] = x; r[AX_Y] = y; r[AX_Z] = z; r[AX_RX] = rx; r[AX_RY] = ry; r[AX_RZ] = rz;
    return r;
}
static RawAxes only(Axis a, int v) {
    RawAxes r{};
    r[a] = v;
    return r;
}

static ModeDef normalMode() {
    ModeDef m;
    m.id = "normal";
    return m;
}

static void run(ModeRunner &r, const Settings &s, const RawAxes &raw, double secs, double dt = DT) {
    for (int i = 0; i < static_cast<int>(secs / dt + 0.5); ++i) r.tick(s, raw, 0, true, dt);
}

// The taps of z held for `secs` with the given keys of the profile.
static int tapsOf(const std::vector<KV> &kv, int z, double secs) {
    Rec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    run(r, conf(kv), only(AX_Z, z), secs);
    return o.taps();
}

// ---------------------------------------------------------------- engine units
static void testRepeaterDefaultsEqualAccumulator() {
    // Without limits and trigger the repeater is exactly the old StepAccumulator behind "rate 0 = reset".
    StepAccumulator acc;
    KeyRepeater rep;
    KeyParams p;
    p.hard_cap = 100.0;
    unsigned seed = 12345;
    for (int i = 0; i < 20000; ++i) {
        seed = seed * 1103515245u + 12345u;
        double u = (seed >> 16) % 7 == 0 ? 0.0 : (double((seed >> 8) % 1600) - 800.0) / 10.0;  // -80 .. +80 steps/s, some rest
        int old = 0;
        if (u == 0.0) acc.reset();
        else old = acc.add(u, DT);
        CHECK(rep.step(u, DT, p) == old, "sample %d u=%g", i, u);
    }
}

static void testRepeaterFirstPress() {
    KeyRepeater rep;
    KeyParams p;
    // old behavior: the first press comes after 1/rate (20 steps/s = 12.5 ticks at 250 Hz)
    int first = -1;
    for (int i = 0; i < 40 && first < 0; ++i)
        if (rep.step(20.0, DT, p) == 1) first = i;
    CHECK(first == 12, "default first press after the delay, got tick %d", first);
    // a lower rate limit: the first press comes at once, then at the limit
    rep.reset();
    p.rate_min = 3.0;
    CHECK(rep.step(0.5, DT, p) == 1, "rate_min > 0: first press in the first tick");
    int n = 0;
    for (int i = 0; i < 250; ++i) n += rep.step(0.5, DT, p);
    CHECK(n >= 2 && n <= 4, "rate_min 3: about 3 presses in one second, got %d", n);
    // rest, then leaving rest presses again at once
    CHECK(rep.step(0.0, DT, p) == 0, "rest");
    CHECK(rep.step(0.5, DT, p) == 1, "first press again when leaving rest");
    // negative side
    rep.reset();
    CHECK(rep.step(-0.5, DT, p) == -1, "first press on the negative side");
}

static void testRepeaterRates() {
    KeyParams p;
    p.hard_cap = 100.0;
    auto perSecond = [&](double u, KeyParams q) {
        KeyRepeater rep;
        int n = 0;
        for (int i = 0; i < 250; ++i) n += std::abs(rep.step(u, DT, q));
        return n;
    };
    CHECK(perSecond(10.0, p) == 10 || perSecond(10.0, p) == 9, "rate follows the deflection (10)");
    CHECK(perSecond(40.0, p) >= 39 && perSecond(40.0, p) <= 40, "rate follows the deflection (40)");
    CHECK(perSecond(40.0, p) > 3 * perSecond(10.0, p), "more deflection, more presses");
    KeyParams mx = p;
    mx.rate_max = 8.0;
    CHECK(perSecond(40.0, mx) >= 7 && perSecond(40.0, mx) <= 8, "rate_max clamps (got %d)", perSecond(40.0, mx));
    CHECK(perSecond(4.0, mx) >= 3 && perSecond(4.0, mx) <= 4, "below rate_max the deflection decides");
    KeyParams mn = p;
    mn.rate_min = 20.0;
    CHECK(perSecond(2.0, mn) >= 20 && perSecond(2.0, mn) <= 21, "rate_min lifts a tiny deflection to the lower limit (got %d)", perSecond(2.0, mn));
    CHECK(perSecond(60.0, mn) >= 59, "above rate_min the deflection decides");
    KeyParams crossed = p;
    crossed.rate_min = 30.0;
    crossed.rate_max = 10.0;
    CHECK(perSecond(5.0, crossed) <= 11, "a lower limit above the upper one never beats it");
    // hard cap: unlimited rate_max still stops at min(100, tick_hz / 2)
    CHECK(perSecond(5000.0, p) >= 99 && perSecond(5000.0, p) <= 100, "hard cap 100 (got %d)", perSecond(5000.0, p));
    Settings s;
    s.tick_hz = 100;
    CHECK(keyParamsOf(s, KM_V_PUSH).hard_cap == 50.0, "hard cap is tick_hz / 2 for a slow tick");
    s.tick_hz = 1000;
    CHECK(keyParamsOf(s, KM_V_PUSH).hard_cap == 100.0, "hard cap never above 100");
}

static void testRepeaterTickCap() {
    KeyRepeater rep;
    KeyParams p;
    p.hard_cap = 1000.0;
    CHECK(rep.step(1e4, 1.0, p) == kMaxPressesPerFunction, "at most 4 presses per tick (was 200)");
    CHECK(rep.step(-1e4, 1.0, p) == -kMaxPressesPerFunction, "also on the negative side");
    CHECK(kMaxPressesPerFunction == 4 && kMaxPressesPerTick == 8, "the caps of the plan");
}

static void testRepeaterTrigger() {
    KeyRepeater rep;
    KeyParams p;
    p.trigger = 20.0;
    p.hard_cap = 100.0;
    for (int i = 0; i < 50; ++i) CHECK(rep.step(15.0, DT, p) == 0, "below the trigger nothing happens");
    CHECK(rep.step(25.0, DT, p) == 1, "crossing the trigger presses at once");
    int n = 0;
    for (int i = 0; i < 250; ++i) n += rep.step(12.0, DT, p);  // above half the trigger: still deflected
    CHECK(n >= 11 && n <= 12, "keeps repeating above half the trigger, got %d", n);
    CHECK(rep.step(9.0, DT, p) == 0, "at half the trigger the motion rests");
    for (int i = 0; i < 50; ++i) CHECK(rep.step(15.0, DT, p) == 0, "below the trigger again: nothing until it is crossed");
    CHECK(rep.step(25.0, DT, p) == 1, "crossing again presses at once");
}

static void testDigitalTrigger() {
    DigitalTrigger d;
    const double T = 20.0;
    CHECK(d.step(0, T) == 0 && d.step(19.9, T) == 0, "below the trigger");
    CHECK(d.step(20.0, T) == 1, "one press at the trigger");
    for (int i = 0; i < 100; ++i) CHECK(d.step(80.0, T) == 0, "holding presses nothing more");
    // jitter between 0.6 T and 1.2 T never re-arms (re-arm at half)
    for (int i = 0; i < 100; ++i) CHECK(d.step(i % 2 ? 0.6 * T : 1.2 * T, T) == 0, "crosstalk jitter does not fire twice");
    CHECK(d.step(10.0, T) == 0, "at half the trigger it re-arms");
    CHECK(d.step(21.0, T) == 1, "and the next crossing presses again");
    // the other side: its own key, and it fires in the same tick as the first side re-arms
    CHECK(d.step(-21.0, T) == -1, "the opposite side presses the other key");
    CHECK(d.step(-5.0, T) == 0, "re-armed");
    CHECK(d.step(0.0, T) == 0 && d.step(-30.0, T) == -1, "negative side fires again after rest");
    d.reset();
    CHECK(d.step(30.0, T) == 1, "reset arms again");
}

// ---------------------------------------------------------------- runner scenarios
static void testScenarioAnalog() {
    // V_ACTION on the unsplit channel: the rate grows with the deflection
    const int low = tapsOf({{"V_ACTION", "keys:up,down"}}, 80, 1.0), high = tapsOf({{"V_ACTION", "keys:up,down"}}, 300, 1.0);
    CHECK(low > 0 && high > 2 * low, "rate grows with deflection: %d vs %d", low, high);
    // the rate clamps (the unsplit channel uses the push values)
    const int capped = tapsOf({{"V_ACTION", "keys:up,down"}, {"SCROLL_V_PUSH_KEY_RATE_MAX", "5"}}, 300, 1.0);
    CHECK(capped >= 4 && capped <= 5, "KEY_RATE_MAX 5: %d presses in a second", capped);
    const int lifted = tapsOf({{"V_ACTION", "keys:up,down"}, {"SCROLL_V_PUSH_KEY_RATE_MIN", "10"}}, 35, 1.0);
    CHECK(lifted >= 9 && lifted <= 11, "KEY_RATE_MIN 10 lifts a light push: %d presses", lifted);
    // the first press: at once with a lower limit, after the delay without
    {
        Rec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_ACTION", "keys:up,down"}, {"SCROLL_V_PUSH_KEY_RATE_MIN", "3"}});
        r.tick(s, only(AX_Z, 300), 0, true, DT);
        CHECK(o.count("tap:down") == 1, "first press in the first tick with KEY_RATE_MIN");
    }
    {
        Rec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        r.tick(conf({{"V_ACTION", "keys:up,down"}}), only(AX_Z, 300), 0, true, DT);
        CHECK(o.taps() == 0, "without limits the first press waits 1/rate");
    }
    // negative direction, split parts, the other motions: push and tilt separately, twist
    {
        Rec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_PUSH_ACTION", "keys:pageup,pagedown"}, {"V_TILT_ACTION", "keys:home,end"}, {"TWIST_ACTION", "keys:left,right"},
                                 {"SCROLL_V_TILT_KEY_RATE_MAX", "2"}, {"ZOOM_KEY_RATE_MAX", "4"}});
        run(r, s, only(AX_Z, -300), 1.0);
        CHECK(o.count("tap:pageup") > 20 && o.count("tap:end") == 0, "push alone: only the push key");
        o.ev.clear();
        run(r, s, only(AX_RX, 300), 1.0);
        CHECK(o.count("tap:end") >= 1 && o.count("tap:end") <= 2 && o.count("tap:pagedown") == 0, "tilt alone with its own rate max");
        o.ev.clear();
        run(r, s, only(AX_RY, -300), 1.0);
        CHECK(o.count("tap:left") >= 3 && o.count("tap:left") <= 4, "twist keys with ZOOM_KEY_RATE_MAX 4: %d", o.count("tap:left"));
    }
}

static void testScenarioDigital() {
    // exactly one press per crossing, the trigger of the motion beats STEP_TRIGGER
    auto presses = [](const std::vector<int> &z, const std::vector<KV> &kv) {
        Rec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf(kv);
        for (int v : z)
            for (int i = 0; i < 10; ++i) r.tick(s, only(AX_Z, v), 0, true, DT);
        return o.taps();
    };
    const std::vector<KV> kv = {{"V_ACTION", "step:left,right"}};  // STEP_TRIGGER 20 = raw 110 (sens 5, dead zone 30)
    CHECK(presses({0, 300, 300, 300, 0}, kv) == 1, "one press for a long push");
    CHECK(presses({0, 300, 0, 300, 0, 300, 0}, kv) == 3, "one press per crossing");
    CHECK(presses({100, 100, 100}, kv) == 0, "below the trigger nothing");
    // re-arm at half: dropping to 0.6 T does not arm, dropping to T/2 does
    CHECK(presses({300, 126, 78, 126, 78, 126}, kv) == 1, "jitter between 0.6 T and 1.2 T presses once");
    CHECK(presses({300, 62, 300}, kv) == 2, "at half the trigger the side re-arms");
    CHECK(presses({300, -300, 300}, kv) == 3, "the other side fires its own key and re-arms on its own");
    // the trigger of the motion
    std::vector<KV> low = kv;
    low.push_back({"SCROLL_V_PUSH_TRIGGER", "5"});
    CHECK(presses({60}, kv) == 0 && presses({60}, low) == 1, "SCROLL_V_PUSH_TRIGGER lowers the level of the motion");
    std::vector<KV> high = kv;
    high.push_back({"SCROLL_V_PUSH_TRIGGER", "60"});
    CHECK(presses({300}, high) == 1 && presses({150}, high) == 0, "and raises it");
    // crosstalk push / tilt: each motion fires its own key once
    {
        Rec o;
        ModeRunner r(&o);
        r.setMode(normalMode());
        const Settings s = conf({{"V_PUSH_ACTION", "step:left,right"}, {"V_TILT_ACTION", "step:up,down"}});
        for (int i = 0; i < 100; ++i) r.tick(s, axes(0, 0, 300, i % 2 ? 78 : 126, 0, 0), 0, true, DT);  // tilt jitters around the trigger
        CHECK(o.count("tap:right") == 1 && o.count("tap:down") == 1, "push %d, tilt %d: one press each", o.count("tap:right"), o.count("tap:down"));
    }
}

static void testScenarioNone() {
    Rec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    for (const char *kind : {"keys", "step"}) {
        o.ev.clear();
        r.releaseAll();
        const Settings s = conf({{"V_ACTION", std::string(kind) + ":none,down"}});
        run(r, s, only(AX_Z, -300), 0.6);
        CHECK(o.ev.empty(), "%s: none on the negative side presses nothing", kind);
        run(r, s, only(AX_Z, 300), 0.6);
        CHECK(o.count("tap:down") >= 1 && o.taps() == o.count("tap:down"), "%s: the positive side still works", kind);
    }
    o.ev.clear();
    r.releaseAll();
    run(r, conf({{"V_ACTION", "step:up,none"}}), only(AX_Z, 300), 0.6);
    CHECK(o.ev.empty(), "step: none on the positive side presses nothing, and no wheel either");
}

static void testScenarioTickCaps() {
    // five functions deflected with a slow tick (huge dt): 4 presses each at most, 8 in all
    Rec o;
    ModeRunner r(&o);
    r.setMode(normalMode());
    const Settings s = conf({{"V_PUSH_ACTION", "keys:up,down"}, {"V_TILT_ACTION", "keys:pageup,pagedown"}, {"H_PUSH_ACTION", "keys:left,right"},
                             {"H_TILT_ACTION", "keys:home,end"}, {"TWIST_ACTION", "keys:minus,plus"}, {"TICK_HZ", "30"}});
    r.tick(s, axes(300, 0, 300, 300, 0, -300), 0, true, 0.5);  // all but twist (ry) deflected, 0.5 s in one tick
    CHECK(o.taps() <= kMaxPressesPerTick && o.taps() >= 6, "at most 8 presses per tick in total, got %d", o.taps());
    for (const char *k : {"tap:up", "tap:down", "tap:left", "tap:right", "tap:pageup", "tap:pagedown", "tap:home", "tap:end"})
        CHECK(o.count(k) <= kMaxPressesPerFunction, "%s at most 4 per tick", k);
    // a single function: 4
    Rec o2;
    ModeRunner r2(&o2);
    r2.setMode(normalMode());
    r2.tick(conf({{"V_ACTION", "keys:up,down"}, {"TICK_HZ", "30"}}), only(AX_Z, 350), 0, true, 1.0);
    CHECK(o2.taps() == 4, "one function: 4 presses per tick, got %d", o2.taps());
}

// ---------------------------------------------------------------- parser: tier rule, none per side
static bool parses(const std::string &t, std::string *err = nullptr) {
    ChannelAction a;
    std::string e;
    const bool ok = parseChannelAction(t, &a, &e);
    if (err) *err = e;
    return ok;
}

static void testTierRule() {
    // tier A: analog allowed
    for (const char *ok : {"keys:up,down", "keys:left,right", "keys:pageup,pagedown", "keys:home,end", "keys:tab,tab",
                           "keys:ctrl+pageup,ctrl+pagedown", "keys:shift+left,shift+right", "keys:ctrl+shift+tab,ctrl+tab",
                           "keys:ctrl+plus,ctrl+minus", "keys:plus,minus", "keys:kp_plus,kp_minus", "keys:volume_up,volume_down",
                           "keys:media_next,media_prev", "keys:ctrl+left,none", "keys:none,pagedown"}) {
        std::string err;
        CHECK(parses(ok, &err), "tier A is allowed analog: %s (%s)", ok, err.c_str());
    }
    // tier B: refused with the documented message
    for (const char *bad : {"keys:a,b", "keys:j,l", "keys:0,1", "keys:f5,f6", "keys:enter,enter", "keys:delete,backspace", "keys:esc,esc",
                            "keys:space,space", "keys:alt+tab,alt+shift+tab", "keys:super+tab,super+tab", "keys:alt+left,alt+right",
                            "keys:play_pause,play_pause", "keys:ctrl,shift", "keys:ctrl+w,ctrl+q", "keys:ctrl+0,ctrl+0",
                            "keys:left,a", "keys:a,none", "keys:none,enter", "keys:ctrl+volume_up,ctrl+volume_down", "keys:volume_mute,volume_up",
                            "keys:media_stop,media_next", "keys:insert,insert"}) {
        std::string err;
        CHECK(!parses(bad, &err) && err == "this key can only be used digitally, use step:", "tier B refused analog: %s (%s)", bad, err.c_str());
    }
    // the same keys digitally: allowed (the load warning is the only protection)
    for (const char *ok : {"step:a,b", "step:j,l", "step:enter,enter", "step:alt+tab,alt+shift+tab", "step:play_pause,play_pause", "step:ctrl,shift",
                           "step:ctrl+w,ctrl+q", "step:f5,none", "step:none,space"})
        CHECK(parses(ok), "tier B is allowed digital: %s", ok);
    // lift: never an analog key (already refused), digital allowed
    ChannelAction a;
    std::string err;
    CHECK(!parseLiftAction("keys:up,down", &a, &err) && !err.empty(), "lift: no analog key");
    CHECK(parseLiftAction("step:up,none", &a, &err), "lift: digital key allowed");
    // the tier function itself
    CHECK(keyAnalogAllowed(Key::PageUp, ChordMods()) && !keyAnalogAllowed(Key::A, ChordMods()) && !keyAnalogAllowed(Key::Space, ChordMods()),
          "keyAnalogAllowed");
}

static void testNonePerSide() {
    ChannelAction a;
    std::string err;
    CHECK(parseChannelAction("step:none,pagedown", &a, &err) && a.negNone && !a.posNone && a.pos == Key::PageDown, "none on the negative side");
    CHECK(parseChannelAction("keys:ctrl+left, NONE", &a, &err) && !a.negNone && a.posNone && a.negMods.n == 1, "none on the positive side, any case");
    CHECK(channelActionText(a) == "keys:ctrl+left,none", "text of a none side: %s", channelActionText(a).c_str());
    CHECK(parseChannelAction("step:none,pagedown", &a, &err) && channelActionText(a) == "step:none,pagedown", "round trip");
    ChannelAction b;
    CHECK(parseChannelAction("step:none,pagedown", &b, nullptr) && a == b, "equal");
    CHECK(parseChannelAction("step:up,pagedown", &b, nullptr) && !(a == b), "a none side differs from a key");
    CHECK(!parses("step:none,none", &err) && !err.empty(), "a key on at least one side");
    CHECK(!parses("keys:none,none") && !parses("keys:none") && !parses("step:,none"), "malformed");
    // a none side does not count for the chord warning
    ChannelAction w;
    CHECK(parseChannelAction("step:none,left", &w, nullptr), "none and a key");
}

// ---------------------------------------------------------------- the keys in profiles and joystick modes
static bool applies(const std::string &key, const std::string &value, std::string *err = nullptr) {
    Settings s;
    return applyKey(s, key, value, err);
}

static void testKeyTuningKeys() {
    const std::vector<std::string> keys = keyTuningKeys();
    CHECK(keys.size() == 18, "18 keys, got %zu", keys.size());
    for (const std::string &k : keys) {
        CHECK(applies(k, "7"), "%s accepts a number", k.c_str());
        std::string err;
        CHECK(!applies(k, "abc", &err) && !err.empty(), "%s refuses text", k.c_str());
        CHECK(!applies(k, "", &err), "%s refuses an empty value", k.c_str());
        if (k.find("KEY_RATE") != std::string::npos) {
            CHECK(!applies(k, "-1", &err) && !applies(k, "1000.5", &err), "%s refuses values outside 0..1000", k.c_str());
            CHECK(applies(k, "0") && applies(k, "1000"), "%s accepts 0 and 1000", k.c_str());
        }
        std::string lower = k;
        for (char &c : lower) c = char(std::tolower(c));
        CHECK(applies(lower, "7"), "%s is case-insensitive", k.c_str());
    }
    // the expected names
    for (const char *p : {"SCROLL_V_PUSH_", "SCROLL_V_TILT_", "SCROLL_H_PUSH_", "SCROLL_H_TILT_", "ZOOM_", "LIFT_"})
        for (const char *f : {"KEY_RATE_MIN", "KEY_RATE_MAX", "TRIGGER"}) {
            bool found = false;
            for (const std::string &k : keys) found = found || k == std::string(p) + f;
            CHECK(found, "%s%s exists", p, f);
        }
    // the values land in the right motion; trigger is clamped like STEP_TRIGGER
    Settings s = conf({{"SCROLL_H_TILT_KEY_RATE_MIN", "3"}, {"SCROLL_H_TILT_KEY_RATE_MAX", "25"}, {"ZOOM_TRIGGER", "0.1"}, {"LIFT_TRIGGER", "50000"}});
    CHECK(s.key_tuning[KM_H_TILT].rate_min == 3.0 && s.key_tuning[KM_H_TILT].rate_max == 25.0 && s.key_tuning[KM_H_PUSH].set == 0, "per motion");
    CHECK(s.key_tuning[KM_TWIST].trigger == kMinStepTrigger && s.key_tuning[KM_LIFT].trigger == kMaxStepTrigger, "trigger clamped");
    // they are written back, only when set, and read back
    const std::vector<KV> all = allKeys(s);
    int n = 0;
    for (const KV &kv : allKeys(Settings()))
        for (const std::string &k : keys) n += kv.first == k;
    CHECK(n == 0, "a default configuration writes none of them");
    Settings back;
    for (const KV &kv : all) {
        std::string err;
        CHECK(applyKey(back, kv.first, kv.second, &err), "%s read back: %s", kv.first.c_str(), err.c_str());
    }
    CHECK(back.key_tuning[KM_H_TILT].rate_max == 25.0 && back.key_tuning[KM_TWIST].trigger == kMinStepTrigger, "round trip");
    // an invalid value leaves the settings unchanged
    Settings keep = conf({{"ZOOM_KEY_RATE_MAX", "9"}});
    CHECK(!applyKey(keep, "ZOOM_KEY_RATE_MAX", "x", nullptr) && keep.key_tuning[KM_TWIST].rate_max == 9.0, "atomic");
    // the old keys still mean what they did
    CHECK(applies("ZOOM_SENS", "5") && applies("SCROLL_V_PUSH_SENS", "5") && applies("LIFT_DEADZONE", "80") && !applies("ZOOM_KEY_RATE", "1"), "neighbours");
}

static void testInProfilesAndModes() {
    // a profile section: the new keys are accepted in every section, the tier rule applies to the action
    ProfileSet ps;
    std::vector<std::string> w;
    parseProfilesText("[global]\nH_TILT_ACTION = keys:ctrl+pageup,ctrl+pagedown\nSCROLL_H_TILT_KEY_RATE_MAX = 8\n"
                      "[device:Test Mouse A]\nZOOM_TRIGGER = 15\n"
                      "[app:x]\nV_ACTION = keys:j,l\nLIFT_ACTION = step:volume_up,volume_down\nLIFT_TRIGGER = 90\nSCROLL_V_PUSH_KEY_RATE_MIN = 3\n",
                      "t", &ps, &w);
    const Settings g = ps.resolve("", "");
    CHECK(g.key_tuning[KM_H_TILT].rate_max == 8.0 && g.actions[kLayerGlobal].partSet[PART_H_TILT], "global profile keys applied");
    const Settings dev = ps.resolve("Test Mouse A", "");
    CHECK(dev.key_tuning[KM_TWIST].trigger == 15.0, "device profile keys applied");
    const Settings app = ps.resolve("", "x");
    CHECK(app.key_tuning[KM_LIFT].trigger == 90.0 && app.key_tuning[KM_V_PUSH].rate_min == 3.0, "program profile keys applied");
    CHECK(!app.actions[kLayerApp].chanSet[ProfileActions::V], "analog tier B in a profile is dropped by the parser");
    bool warned = false;
    for (const std::string &x : w) warned = warned || x.find("this key can only be used digitally, use step:") != std::string::npos;
    CHECK(warned, "with the message of the parser");
    CHECK(app.actions[kLayerApp].chanSet[ProfileActions::LIFT], "lift step: is accepted");

    // a joystick mode file: the same keys as overrides, validated by applyKey
    const std::string dir = tu::makeTempDir("dofscroll-key-engine");
    {
        std::ofstream f(dir + "/keyed.conf");
        f << "H_ACTION = keys:ctrl+pageup,ctrl+pagedown\nSCROLL_H_PUSH_KEY_RATE_MAX = 8\nZOOM_KEY_RATE_MIN = 2\nLIFT_TRIGGER = 70\n"
             "V_ACTION = keys:j,l\nTWIST_ACTION = step:j,l\nSCROLL_V_TILT_TRIGGER = nope\n";
    }
    std::vector<std::string> mw;
    const ModeSet ms = loadModes(dir, &mw);
    const ModeDef *m = ms.find("keyed");
    CHECK(m && m->h.type == ChannelActionType::Keys && m->twist.type == ChannelActionType::Step, "the mode loads");
    CHECK(m && m->v.type == ChannelActionType::Default, "analog tier B refused in a mode file");
    size_t found = 0;
    for (const std::string &x : mw) {
        if (x.find("V_ACTION: this key can only be used digitally, use step:") != std::string::npos) ++found;
        if (x.find("SCROLL_V_TILT_TRIGGER") != std::string::npos && x.find("number expected") != std::string::npos) ++found;
    }
    CHECK(found == 2, "the loader names both problems, found %zu", found);
    bool hasOverride = false;
    if (m)
        for (const auto &o : m->overrides) hasOverride = hasOverride || o.first == "SCROLL_H_PUSH_KEY_RATE_MAX";
    CHECK(hasOverride && m && m->overrides.size() == 3, "the good keys are overrides of the mode (%zu)", m ? m->overrides.size() : size_t(0));
    tu::removeAll(dir);
}

int main() {
    testRepeaterDefaultsEqualAccumulator();
    testRepeaterFirstPress();
    testRepeaterRates();
    testRepeaterTickCap();
    testRepeaterTrigger();
    testDigitalTrigger();
    testScenarioAnalog();
    testScenarioDigital();
    testScenarioNone();
    testScenarioTickCaps();
    testTierRule();
    testNonePerSide();
    testKeyTuningKeys();
    testInProfilesAndModes();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_key_engine: all passed");
    return 0;
}
