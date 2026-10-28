// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The lift / press motion (stage 2 of #224): LIFT_ACTION and LIFT_* on the real ModeRunner with a recording output.
// The axis is whatever LIFT_MIX says (axis_convention.hpp: y with spacenavd, z on Windows and macOS), never a fixed name.
// Default behaviour (action none) is covered by tests/test_golden_motions.cpp and tests/test_mode_compat.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "axis_convention.hpp"
#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"

using namespace spn;

struct LiftRec : Output {
    std::vector<std::string> ev;
    int wv = 0, wh = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { wv += n; ev.push_back("wv"); }
    void wheelH(int n) override { wh += n; ev.push_back("wh"); }
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
};

static const double DT = 1.0 / 250.0;

static Settings conf(const std::vector<KV> &kv, int layer = kLayerGlobal) {
    Settings s;
    for (const KV &p : kv) {
        std::string err;
        const bool ok = applyKeyLayer(s, p.first, p.second, layer, &err);
        if (!ok) std::fprintf(stderr, "bad key %s: %s\n", p.first.c_str(), err.c_str());
        assert(ok);
    }
    return s;
}

static void run(ModeRunner &r, const Settings &s, Axis a, int v, double secs, uint32_t btn = 0) {
    RawAxes raw{};
    raw[size_t(a)] = v;
    for (int i = 0; i < int(secs / DT); ++i) r.tick(s, raw, btn, true, DT);
}

int main() {
    // the default lift axis is the one of the Linux convention (the seeds of Windows and macOS say z)
    assert(Settings().lift.mix.terms.size() == 1 && Settings().lift.mix.terms[0].axis == conventionLiftAxis(AxisConvention::Linux));
    assert(conventionLiftAxis(AxisConvention::WindowsDriver) == AX_Z);

    {  // default: lifting does nothing, with or without the lift keys at their defaults
        LiftRec o;
        ModeRunner r(&o);
        const Settings s;
        run(r, s, AX_Y, 400, 0.5);
        assert(o.ev.empty() && r.legacyPath() && !r.axesActive());
        const Settings s2 = conf({{"LIFT_ACTION", "none"}});
        run(r, s2, AX_Y, 400, 0.5);
        assert(o.ev.empty() && r.legacyPath() && !r.axesActive());
    }
    {  // wheel_v: scrolls while lifted above the dead zone (80), nothing below it; LIFT_SIGN flips the direction
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "wheel_v"}});
        run(r, s, AX_Y, 70, 0.5);
        assert(o.ev.empty() && !r.axesActive());
        run(r, s, AX_Y, 400, 0.5);
        assert(o.wv > 0 && r.axesActive() && !r.legacyPath());
        run(r, s, AX_Y, 0, 0.5);
        assert(!r.axesActive());
        LiftRec o2;
        ModeRunner r2(&o2);
        const Settings neg = conf({{"LIFT_ACTION", "wheel_v"}, {"LIFT_SIGN", "-1"}});
        run(r2, neg, AX_Y, 400, 0.5);
        assert(o2.wv < 0);
        // the other axes still do their normal job at the same time (push forward/back z scrolls)
        LiftRec o3;
        ModeRunner r3(&o3);
        run(r3, s, AX_Z, 300, 0.5);
        assert(o3.wv > 0);
    }
    {  // LIFT_SENS and LIFT_DEADZONE: sens 0 turns the motion off, a lower dead zone lets a light lift through
        LiftRec o;
        ModeRunner r(&o);
        run(r, conf({{"LIFT_ACTION", "wheel_v"}, {"LIFT_SENS", "0"}}), AX_Y, 400, 0.5);
        assert(o.ev.empty());
        run(r, conf({{"LIFT_ACTION", "wheel_v"}, {"LIFT_DEADZONE", "20"}}), AX_Y, 70, 0.5);
        assert(o.wv > 0);
    }
    {  // LIFT_MIX names the axis: z here (as on Windows and macOS), y no longer reacts
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "wheel_h"}, {"LIFT_MIX", "z"}});
        run(r, s, AX_Y, 400, 0.5);
        assert(o.wh == 0);
        // (z also scrolls vertically through SCROLL_V_MIX on this configuration; the lift part is the horizontal wheel)
        run(r, s, AX_Z, 400, 0.5);
        assert(o.wh > 0);
    }
    {  // step: one tap per push, re-armed after the lift is released; the other direction taps the other key
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "step:pageup,pagedown"}});
        run(r, s, AX_Y, 600, 1.0);
        assert(o.count("tap:pagedown") == 1 && o.count("tap:pageup") == 0);
        run(r, s, AX_Y, 0, 0.5);
        run(r, s, AX_Y, 600, 1.0);
        assert(o.count("tap:pagedown") == 2);
        run(r, s, AX_Y, 0, 0.5);
        run(r, s, AX_Y, -600, 1.0);
        assert(o.count("tap:pageup") == 1 && o.count("tap:pagedown") == 2);
    }
    {  // zoom: the zoom keys are held while lifted and released afterwards; other wheel motions pause meanwhile
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "zoom"}});
        run(r, s, AX_Y, 400, 0.3);
        assert(o.count("+ctrl") == 1 && o.wv != 0 && r.holdsAnything());
        run(r, s, AX_Y, 0, 0.5);
        assert(o.count("-ctrl") == 1 && !r.holdsAnything());
    }
    {  // safety net paths: releaseAll frees the zoom key and the lift rate is gone; setMode too
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "zoom"}});
        run(r, s, AX_Y, 400, 0.3);
        assert(r.holdsAnything() && r.axesActive());
        r.releaseAll();
        assert(!r.holdsAnything() && o.count("-ctrl") == 1);
        run(r, s, AX_Y, 400, 0.3);
        assert(r.holdsAnything());
        r.setMode(ModeDef());
        assert(!r.holdsAnything() && o.count("-ctrl") == 2);
    }
    {  // a mode's LIFT_ACTION and the program layer
        LiftRec o;
        ModeRunner r(&o);
        ModeDef m;
        m.id = "lifty";
        std::string err;
        assert(parseLiftAction("wheel_v", &m.lift, &err));
        r.setMode(m);
        const Settings none;
        run(r, none, AX_Y, 400, 0.5);
        assert(o.wv > 0);
        LiftRec o2;
        ModeRunner r2(&o2);
        r2.setMode(m);
        const Settings app = conf({{"LIFT_ACTION", "none"}}, kLayerApp);  // the program says none: it wins over the mode
        run(r2, app, AX_Y, 400, 0.5);
        assert(o2.ev.empty());
    }
    {  // the zoom latch of a twist does not let a wheel lift through (same rule as the push and tilt parts)
        LiftRec o;
        ModeRunner r(&o);
        const Settings s = conf({{"LIFT_ACTION", "wheel_v"}});
        RawAxes raw{};
        raw[AX_RY] = 400;
        raw[AX_Y] = 400;
        for (int i = 0; i < 250; ++i) r.tick(s, raw, 0, true, DT);
        assert(o.count("+ctrl") == 1);
        for (int i = 0; i < 100; ++i) r.tick(s, raw, 0, true, DT);
        // the twist keeps zooming (ctrl stays held); the lift wheel pauses instead of scrolling plainly
        assert(o.count("-ctrl") == 0);
    }
    std::puts("test_lift_motion: OK");
    return 0;
}
