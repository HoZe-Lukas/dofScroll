// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for ModeRunner (channel actions, button bindings, release of held state)
// with a fake Output that records all events.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "mode_runner.hpp"

using namespace spn;

struct RunnerRec : Output {
    std::vector<std::string> ev;
    int wv = 0, wh = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { wv += n; ev.push_back("wv" + std::to_string(n)); }
    void wheelH(int n) override { wh += n; ev.push_back("wh" + std::to_string(n)); }
    void setKey(Key k, bool d) override { ev.push_back(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { ev.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override {
        ev.push_back(std::string(d ? "+m" : "-m") + std::to_string(b));
    }
    void releaseAll() override {}
    void flush() override {}
    int count(const std::string &e) const {
        int c = 0;
        for (const auto &x : ev) c += x == e;
        return c;
    }
    void clear() { ev.clear(); wv = wh = 0; }
};

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static const double DT = 1.0 / 250.0;

static void run(ModeRunner &r, const Settings &s, const RawAxes &raw, uint32_t btn, double secs,
                bool fresh = true) {
    for (int i = 0; i < static_cast<int>(secs / DT); ++i) r.tick(s, raw, btn, fresh, DT);
}

static ChannelAction act(ChannelActionType t, Key neg = Key::Up, Key pos = Key::Down) {
    ChannelAction a;
    a.type = t;
    a.neg = neg;
    a.pos = pos;
    return a;
}
static ButtonBinding bind(int b, ButtonActionType t, Key k = Key::Space) {
    ButtonBinding x;
    x.button = b + 1;  // the helper takes the device bit index; bindings are 1-based
    x.type = t;
    x.key = k;
    return x;
}
static ModeDef mode(const char *id) {
    ModeDef m;
    m.id = id;
    m.label = id;
    return m;
}

static RawAxes axis(Axis a, int v) {
    RawAxes r{};
    r[a] = v;
    return r;
}

static void testLegacyBindings() {
    RunnerRec o;
    ModeRunner r(&o);
    ModeDef m = mode("normal");
    m.buttons = {bind(3, ButtonActionType::Key, Key::Space), bind(4, ButtonActionType::RightClick),
                 bind(5, ButtonActionType::MiddleClick), bind(6, ButtonActionType::Click)};
    r.setMode(m);
    CHECK(r.legacyPath(), "all-default must be the legacy path");
    Settings s;
    run(r, s, RawAxes{}, 1u << 3, 0.05);
    CHECK(o.count("+space") == 1, "space pressed once");
    run(r, s, RawAxes{}, (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6), 0.02);
    CHECK(o.count("+m3") == 1 && o.count("+m2") == 1 && o.count("+m1") == 1, "mouse buttons");
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(o.count("-space") == 1 && o.count("-m3") == 1 && o.count("-m2") == 1 && o.count("-m1") == 1,
          "released on button up");
}

static void testLegacyNavAndClick() {
    RunnerRec o;
    ModeRunner r(&o);
    Settings s;
    s.nav_button_enabled = true;
    s.nav_button = 0;
    s.click_button_enabled = true;
    s.click_button = 1;
    run(r, s, axis(AX_Z, 300), 1u << 0, 0.5);
    CHECK(o.count("tap:down") > 0 && o.wv == 0, "NAV_BUTTON converts scroll to arrow keys");
    o.clear();
    run(r, s, RawAxes{}, 1u << 1, 0.02);
    CHECK(o.count("+m1") == 1, "CLICK_BUTTON presses left");
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(o.count("-m1") == 1, "CLICK_BUTTON releases left");
    // nav binding in the mode, no NAV_BUTTON in the settings
    RunnerRec o2;
    ModeRunner r2(&o2);
    ModeDef m = mode("normal");
    m.buttons = {bind(2, ButtonActionType::Nav)};
    r2.setMode(m);
    run(r2, Settings(), axis(AX_Z, 300), 1u << 2, 0.5);
    CHECK(o2.count("tap:down") > 0 && o2.wv == 0, "nav binding");
}

// hold:mode:<id>: the press is reported as "hold:<id>", the release of that button as "release", also
// when the mode that is active while holding does not bind the button.
static void testHoldMode() {
    Settings s;
    ModeDef m = mode("a");
    ButtonBinding b;
    b.button = 3;
    b.type = ButtonActionType::HoldMode;
    b.mode = "fine";
    m.buttons.push_back(b);
    RunnerRec o;
    ModeRunner r(&o);
    r.setMode(m);
    r.tick(s, RawAxes{}, 0u, true, DT);
    CHECK(r.takeSwitchRequest().empty(), "nothing before the press");
    r.tick(s, RawAxes{}, 1u << 2, true, DT);
    CHECK(r.takeSwitchRequest() == "hold:fine", "press reports hold:<id>");
    r.tick(s, RawAxes{}, 1u << 2, true, DT);
    CHECK(r.takeSwitchRequest().empty(), "no repeat while held");
    // the daemon switches the mode: the new mode has no binding for button 3
    r.setMode(mode("fine"));
    r.tick(s, RawAxes{}, 1u << 2, true, DT);
    CHECK(r.takeSwitchRequest().empty(), "still held after the switch");
    r.tick(s, RawAxes{}, 0u, true, DT);
    CHECK(!validModeId(ModeRunner::kHoldReleaseRequest), "the hold-release request cannot be a mode id");
    CHECK(r.takeSwitchRequest() == std::string(ModeRunner::kHoldReleaseRequest), "release is reported although the active mode does not bind the button");
    r.tick(s, RawAxes{}, 0u, true, DT);
    CHECK(r.takeSwitchRequest().empty(), "reported once");
    // another button's release does not end the hold
    r.setMode(m);
    r.tick(s, RawAxes{}, (1u << 2) | 1u, true, DT);
    CHECK(r.takeSwitchRequest() == "hold:fine", "second press");
    r.tick(s, RawAxes{}, 1u << 2, true, DT);  // button 1 released
    CHECK(r.takeSwitchRequest().empty(), "release of another button: nothing");
    // clearToggles forgets the hold: no release is reported
    r.clearToggles();
    r.tick(s, RawAxes{}, 0u, true, DT);
    CHECK(r.takeSwitchRequest().empty(), "cleared hold: no release");
    // press and release of the hold button in consecutive ticks while another request waits
    ModeDef m2 = mode("b");
    ButtonBinding hb = b, sw;
    sw.button = 1;
    sw.type = ButtonActionType::ModeSwitch;
    sw.mode = "x";
    m2.buttons = {hb, sw};
    r.setMode(m2);
    r.tick(s, RawAxes{}, 1u << 2, true, DT);
    CHECK(r.takeSwitchRequest() == "hold:fine", "hold press");
    r.tick(s, RawAxes{}, 1u << 0, true, DT);  // hold released and button 1 (mode:x) pressed in one tick
    const std::string first = r.takeSwitchRequest();
    r.tick(s, RawAxes{}, 1u << 0, true, DT);
    const std::string second = r.takeSwitchRequest();
    CHECK((first == std::string(ModeRunner::kHoldReleaseRequest) && second.empty()) || (first == "x" && second == std::string(ModeRunner::kHoldReleaseRequest)) ||
              (first == std::string(ModeRunner::kHoldReleaseRequest) && second == "x") || first == "x",
          "release is not lost when another request is pending (%s, %s)", first.c_str(), second.c_str());
}

static void testChordChannels() {
    Settings s;
    auto chord = [](Key neg, Key pos, std::initializer_list<Key> mods) {
        ChannelAction a = act(ChannelActionType::Keys, neg, pos);
        for (Key k : mods) {
            a.negMods.k[a.negMods.n++] = k;
            a.posMods.k[a.posMods.n++] = k;
        }
        return a;
    };
    {  // positive direction: modifiers down in the order written, tap, modifiers up in reverse
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = chord(Key::PageUp, Key::PageDown, {Key::Ctrl, Key::Shift});
        m.h = act(ChannelActionType::Off);
        m.twist = act(ChannelActionType::Off);
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.ev.size() >= 4 && o.ev[0] == "+ctrl" && o.ev[1] == "+shift" && o.ev[2] == "tap:pagedown" &&
                  o.ev[3] == "-shift" && o.ev[4] == "-ctrl",
              "chord sequence");
        CHECK(o.count("tap:pageup") == 0, "only the positive key");
        CHECK(o.count("+ctrl") == o.count("-ctrl") && o.count("+shift") == o.count("-shift"),
              "every modifier press is released");
        o.clear();
        run(r, s, axis(AX_Z, -300), 0, 0.5);
        CHECK(o.count("tap:pageup") > 0 && o.count("tap:pagedown") == 0, "negative direction");
        CHECK(o.ev.size() >= 3 && o.ev[0] == "+ctrl", "negative chord starts with the modifier");
    }
    {  // a modifier that a button holds is neither pressed again nor released
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = chord(Key::PageUp, Key::PageDown, {Key::Ctrl});
        m.h = act(ChannelActionType::Off);
        m.twist = act(ChannelActionType::Off);
        m.buttons.push_back(bind(0, ButtonActionType::Key, Key::Ctrl));
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 1u, 0.5);
        CHECK(o.count("+ctrl") == 1 && o.count("-ctrl") == 0 && o.count("tap:pagedown") > 0,
              "held Ctrl stays held, pressed once by the button");
        run(r, s, RawAxes{}, 0u, 0.1);
        CHECK(o.count("-ctrl") == 1, "released with the button");
    }
    {  // zoom on another channel holds Ctrl: the chord does not release it
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = chord(Key::PageUp, Key::PageDown, {Key::Ctrl});
        m.h = act(ChannelActionType::Zoom);
        r.setMode(m);
        RawAxes both = axis(AX_Z, 300);
        both[AX_X] = 300;
        run(r, s, both, 0, 0.5);
        CHECK(o.count("tap:pagedown") > 0 && o.count("+ctrl") == 1 && o.count("-ctrl") == 0, "zoom Ctrl stays");
    }
    {  // INVERT_AXES: the direction of a pushed axis flips for every mode, tilt stays
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(mode("t"));
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        const int normal = o.wv;
        CHECK(normal != 0, "push scrolls");
        o.clear();
        Settings si = s;
        si.invert_axes = 1u << AX_Z;
        run(r, si, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.wv == -normal, "inverted push scrolls the other way");
        o.clear();
        run(r, si, axis(AX_RX, 300), 0, 0.5);
        CHECK(o.wv == normal, "tilt unchanged");
    }
    {  // ZOOM_MODIFIER: the keys held while zooming come from the settings
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::Zoom);
        r.setMode(m);
        Settings sm = s;
        sm.zoom_modifier = kZoomModSuper | kZoomModShift;
        run(r, sm, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("+super") == 1 && o.count("+shift") == 1 && o.count("+ctrl") == 0 && o.count("wv1") + o.count("wv-1") > 0,
              "zoom holds the configured keys");
        run(r, sm, RawAxes{}, 0, 0.5);
        CHECK(o.count("-super") == 1 && o.count("-shift") == 1, "released again");
        o.clear();
        sm.zoom_modifier = 0;  // none: the wheel alone
        run(r, sm, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 0 && o.count("+super") == 0 && o.wv != 0, "no key held");
    }
    {  // plain keys behave as before: no modifier events
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::Keys, Key::PageUp, Key::PageDown);
        m.h = act(ChannelActionType::Off);
        m.twist = act(ChannelActionType::Off);
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("tap:pagedown") > 0 && o.count("+ctrl") == 0 && o.count("+shift") == 0, "plain keys");
    }
}

static void testStepChannels() {
    Settings s;
    s.scroll_v.deadzone = 0;
    s.scroll_v.smooth_ms = 0.0;
    s.step_trigger = 20.0;  // rate = deflection * 5 / 2500 * 125 = deflection / 4: fires at 80, re-arms at 40
    auto stepMode = [](Key neg, Key pos) {
        ModeDef m = mode("st");
        m.v = act(ChannelActionType::Step, neg, pos);
        m.h = act(ChannelActionType::Off);
        m.twist = act(ChannelActionType::Off);
        return m;
    };
    {  // a sustained push fires exactly once, even when held for seconds
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        CHECK(!r.legacyPath(), "step is a custom-path mode");
        run(r, s, axis(AX_Z, 300), 0, 3.0);
        CHECK(o.count("tap:pagedown") == 1 && o.count("tap:pageup") == 0 && o.ev.size() == 1, "one tap while held");
        // release and push again fires again
        run(r, s, axis(AX_Z, 0), 0, 0.1);
        run(r, s, axis(AX_Z, 300), 0, 1.0);
        CHECK(o.count("tap:pagedown") == 2, "second push, second tap");
    }
    {  // hysteresis: oscillation between the trigger and half of it does not fire again
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r, s, axis(AX_Z, 90), 0, 0.2);   // rate 22.5 >= 20: fires
        run(r, s, axis(AX_Z, 60), 0, 0.2);   // rate 15, above half (10): still latched
        run(r, s, axis(AX_Z, 90), 0, 0.2);
        run(r, s, axis(AX_Z, 60), 0, 0.2);
        run(r, s, axis(AX_Z, 90), 0, 0.2);
        CHECK(o.count("tap:pagedown") == 1, "oscillation above the re-arm level fires once");
        run(r, s, axis(AX_Z, 30), 0, 0.2);   // rate 7.5 <= 10: re-armed
        run(r, s, axis(AX_Z, 90), 0, 0.2);
        CHECK(o.count("tap:pagedown") == 2, "fires again after falling to half the trigger");
    }
    {  // a push barely outside the dead zone does not fire
        RunnerRec o;
        ModeRunner r(&o);
        Settings d;  // default dead zone and sensitivity
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r, d, axis(AX_Z, 45), 0, 1.0);
        CHECK(o.ev.empty(), "small deflection: no tap");
        run(r, d, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("tap:pagedown") == 1, "deliberate push with the defaults fires");
    }
    {  // reversal: the other direction fires, one tap per side
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        run(r, s, axis(AX_Z, -300), 0, 0.5);
        CHECK(o.count("tap:pagedown") == 1 && o.count("tap:pageup") == 1, "reversal fires the other key once");
        run(r, s, axis(AX_Z, 0), 0, 0.1);
        run(r, s, axis(AX_Z, -300), 0, 0.5);
        CHECK(o.count("tap:pageup") == 2, "negative side re-arms through the center");
    }
    {  // a mode change resets the state: the held push fires again in the new mode
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("tap:pagedown") == 1, "fired");
        r.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("tap:pagedown") == 2, "state reset by the mode change");
        r.releaseAll();
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("tap:pagedown") == 3, "state reset by releaseAll");
    }
    {  // a chord is tapped with its modifiers; a larger trigger needs a firmer push
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = stepMode(Key::PageUp, Key::PageDown);
        m.v.posMods.k[0] = Key::Ctrl;
        m.v.posMods.n = 1;
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.ev.size() == 3 && o.ev[0] == "+ctrl" && o.ev[1] == "tap:pagedown" && o.ev[2] == "-ctrl", "chord step");
        Settings firm = s;
        firm.step_trigger = 200.0;
        RunnerRec o2;
        ModeRunner r2(&o2);
        r2.setMode(stepMode(Key::PageUp, Key::PageDown));
        run(r2, firm, axis(AX_Z, 300), 0, 0.5);
        CHECK(o2.ev.empty(), "300/4 = 75 is below a trigger of 200");
    }
    {  // twist and horizontal channels work the same way
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("st");
        m.v = act(ChannelActionType::Off);
        m.h = act(ChannelActionType::Step, Key::Left, Key::Right);
        m.twist = act(ChannelActionType::Step, Key::Home, Key::End);
        r.setMode(m);
        Settings d;
        run(r, d, axis(AX_X, 300), 0, 1.0);
        CHECK(o.count("tap:right") == 1, "H step");
        run(r, d, axis(AX_RY, 300), 0, 1.0);
        CHECK(o.count("tap:end") == 1 || o.count("tap:home") == 1, "twist step");
    }
}

static void testCustomChannels() {
    Settings s;
    {  // V none, H keys left/right
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::Off);
        m.h = act(ChannelActionType::Keys, Key::Left, Key::Right);
        r.setMode(m);
        CHECK(!r.legacyPath(), "custom path");
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.ev.empty(), "V none: nothing sent");
        run(r, s, axis(AX_X, 300), 0, 0.5);
        CHECK(o.count("tap:right") > 0 && o.count("tap:left") == 0 && o.wh == 0, "H keys right");
        o.clear();
        run(r, s, axis(AX_X, -300), 0, 0.5);
        CHECK(o.count("tap:left") > 0 && o.count("tap:right") == 0, "H keys left");
    }
    {  // twist -> keys; default zoom replaced
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.twist = act(ChannelActionType::Keys, Key::Left, Key::Right);
        r.setMode(m);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.count("tap:right") > 0 && o.count("+ctrl") == 0, "twist keys, no Ctrl");
        o.clear();
        run(r, s, axis(AX_RY, -300), 0, 0.5);
        CHECK(o.count("tap:left") > 0, "twist keys negative");
    }
    {  // V -> wheel_h swaps the wheel
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::WheelH);
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.wh > 0 && o.wv == 0, "V as horizontal wheel");
    }
    {  // V -> zoom: Ctrl held while deflected, released after
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::Zoom);
        r.setMode(m);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 1 && o.wv > 0, "V zoom: Ctrl + wheel");
        run(r, s, RawAxes{}, 0, 0.2);
        CHECK(o.count("-ctrl") == 1, "Ctrl released at rest");
    }
    {  // all none ("off"): nothing, even with CLICK_BUTTON in the settings (custom path)
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("off");
        m.v = m.h = m.twist = act(ChannelActionType::Off);
        r.setMode(m);
        Settings sc;
        sc.click_button_enabled = true;
        run(r, sc, axis(AX_Z, 300), 1u << 1, 0.5);
        run(r, sc, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.ev.empty(), "off mode sends nothing");
    }
    {  // "off" switches the axes off, not the buttons: BUTTON_<n> of the profile keeps working (independent of the
       // joystick mode); only ENABLED = 0 silences the buttons too
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("off");
        m.v = m.h = m.twist = act(ChannelActionType::Off);
        r.setMode(m);
        Settings sb;
        sb.buttons[1] = "key:space";
        sb.buttons[2] = "rightclick";
        run(r, sb, axis(AX_Z, 300), (1u << 0) | (1u << 1), 0.5);
        CHECK(o.count("+space") == 1 && o.count("+m3") == 1, "off keeps the profile's buttons");
        CHECK(o.wv == 0 && o.wh == 0, "off still sends no scrolling");
    }
    {  // twist default in a custom mode keeps the dominance zoom
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.v = act(ChannelActionType::Off);
        r.setMode(m);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 1 && o.wv < 0, "twist default zooms in");
    }
}

static void testReleases() {
    Settings s;
    {  // mode change releases a held key binding and mouse button
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("a");
        m.buttons = {bind(0, ButtonActionType::Key, Key::Space), bind(1, ButtonActionType::Click)};
        r.setMode(m);
        run(r, s, RawAxes{}, 3u, 0.05);
        o.clear();
        r.setMode(mode("b"));
        CHECK(o.count("-space") == 1 && o.count("-m1") == 1, "mode change releases");
        run(r, s, RawAxes{}, 3u, 0.05);  // buttons still held, but mode b has no bindings
        CHECK(o.count("+space") == 0 && o.count("+m1") == 0, "no stale bindings");
    }
    {  // releaseAll while zooming releases Ctrl
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 1, "zooming");
        r.releaseAll();
        CHECK(o.count("-ctrl") == 1, "releaseAll releases Ctrl");
        r.releaseAll();
        CHECK(o.count("-ctrl") == 1, "releaseAll is idempotent");
    }
    {  // resetDsp releases Ctrl but not the mouse button
        RunnerRec o;
        ModeRunner r(&o);
        Settings sc;
        sc.click_button_enabled = true;
        sc.click_button = 1;
        run(r, sc, axis(AX_RY, 300), 1u << 1, 0.5);
        r.resetDsp();
        CHECK(o.count("-ctrl") == 1 && o.count("-m1") == 0, "resetDsp keeps the drag");
    }
}

static void testModeSwitchRequest() {
    RunnerRec o;
    ModeRunner r(&o);
    ModeDef m = mode("normal");
    ButtonBinding b = bind(1, ButtonActionType::ModeSwitch);
    b.mode = "next";
    m.buttons = {b, bind(2, ButtonActionType::Key, Key::Space)};
    r.setMode(m);
    Settings s;
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(r.takeSwitchRequest().empty(), "no request without a press");
    run(r, s, RawAxes{}, 1u << 1, 0.1);  // held for many ticks: one request only
    CHECK(r.takeSwitchRequest() == "next", "press requests the switch");
    CHECK(r.takeSwitchRequest().empty(), "taking clears the request");
    run(r, s, RawAxes{}, 1u << 1, 0.1);
    CHECK(r.takeSwitchRequest().empty(), "holding does not repeat");
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(r.takeSwitchRequest().empty(), "release does not switch");
    // a key held by another binding is released by the switch (setMode releases everything)
    run(r, s, RawAxes{}, 1u << 2, 0.02);
    CHECK(o.count("+space") == 1, "space held");
    r.setMode(mode("other"));
    CHECK(o.count("-space") == 1, "space released on mode change");
}

static void testToggleNav() {
    RunnerRec o;
    ModeRunner r(&o);
    ModeDef m = mode("normal");
    m.buttons = {bind(2, ButtonActionType::ToggleNav)};
    r.setMode(m);
    Settings s;
    CHECK(!r.navLatched(), "off at the start");
    run(r, s, axis(AX_Z, 300), 0, 0.3);
    CHECK(o.wv != 0 && o.count("tap:down") == 0, "scroll before the toggle");
    o.clear();
    run(r, s, RawAxes{}, 1u << 2, 0.1);  // press, held for many ticks: one flip only
    CHECK(r.navLatched(), "press latches nav");
    run(r, s, RawAxes{}, 0, 0.02);       // released: the latch stays
    CHECK(r.navLatched(), "release keeps the latch");
    run(r, s, axis(AX_Z, 300), 0, 0.5);
    CHECK(o.count("tap:down") > 0 && o.wv == 0, "arrow keys while latched");
    run(r, s, RawAxes{}, 1u << 2, 0.1);  // second press
    CHECK(!r.navLatched(), "second press unlatches");
    o.clear();
    run(r, s, RawAxes{}, 0, 0.02);
    run(r, s, axis(AX_Z, 300), 0, 0.3);
    CHECK(o.wv != 0 && o.count("tap:down") == 0, "scroll again after the second press");
    // the safety net's releaseAll() keeps the latch; clearToggles() and a mode change clear it
    run(r, s, RawAxes{}, 1u << 2, 0.02);
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(r.navLatched(), "latched again");
    r.releaseAll();
    CHECK(r.navLatched(), "releaseAll keeps the latch");
    r.clearToggles();
    CHECK(!r.navLatched(), "clearToggles clears it");
    run(r, s, RawAxes{}, 1u << 2, 0.02);
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(r.navLatched(), "latched once more");
    r.setMode(m);
    CHECK(!r.navLatched(), "a mode change clears it");
}

// A held nav button and the latch are ORed; toggle:nav also works as BUTTON_<n> of the settings.
static void testToggleNavFromSettings() {
    RunnerRec o;
    ModeRunner r(&o);
    Settings s;
    s.buttons[3] = "toggle:nav";
    run(r, s, RawAxes{}, 1u << 2, 0.02);
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(r.navLatched(), "settings BUTTON_3 = toggle:nav latches");
    // the mode's own binding for the same button wins
    RunnerRec o2;
    ModeRunner r2(&o2);
    ModeDef m = mode("normal");
    m.buttons = {bind(2, ButtonActionType::Key, Key::Space)};
    r2.setMode(m);
    run(r2, s, RawAxes{}, 1u << 2, 0.02);
    run(r2, s, RawAxes{}, 0, 0.02);
    CHECK(!r2.navLatched(), "mode binding wins over the settings toggle");
}

static void testToggleModeRequest() {
    RunnerRec o;
    ModeRunner r(&o);
    ModeDef m = mode("normal");
    ButtonBinding b = bind(1, ButtonActionType::ToggleMode);
    b.mode = "timeline";
    m.buttons = {b};
    r.setMode(m);
    Settings s;
    run(r, s, RawAxes{}, 1u << 1, 0.1);
    CHECK(r.takeSwitchRequest() == "toggle:timeline", "press requests the toggle");
    run(r, s, RawAxes{}, 1u << 1, 0.1);
    CHECK(r.takeSwitchRequest().empty(), "holding does not repeat");
    // mode:<id> from the settings requests a switch too (it did not before)
    RunnerRec o2;
    ModeRunner r2(&o2);
    Settings s2;
    s2.buttons[1] = "mode:timeline";
    run(r2, s2, RawAxes{}, 1u << 0, 0.1);
    CHECK(r2.takeSwitchRequest() == "timeline", "settings BUTTON_1 = mode:timeline requests the switch");
}

// BUTTON_<n> from the resolved Settings works; the mode's binding for the same n wins.
static void testSettingsButtons() {
    RunnerRec o;
    ModeRunner r(&o);
    Settings s;
    s.buttons[1] = "key:space";
    s.buttons[2] = "rightclick";
    run(r, s, RawAxes{}, 1u << 0, 0.02);
    CHECK(o.count("+space") == 1, "settings BUTTON_1 holds space");
    run(r, s, RawAxes{}, 0, 0.02);
    CHECK(o.count("-space") == 1, "released");
    run(r, s, RawAxes{}, 1u << 1, 0.02);
    CHECK(o.count("+m3") == 1, "settings BUTTON_2 right click");
    run(r, s, RawAxes{}, 0, 0.02);
    ModeDef m = mode("m");
    m.buttons.push_back(bind(1, ButtonActionType::Click));  // button 2 -> left click
    r.setMode(m);
    o.clear();
    run(r, s, RawAxes{}, 1u << 1, 0.02);
    CHECK(o.count("+m1") == 1 && o.count("+m3") == 0, "mode binding overrides settings");
}

static void testChordButtons() {
    // a key chord on a button (key:ctrl+0) is tapped once on the press edge and does nothing on release
    {
        RunnerRec o;
        ModeRunner r(&o);
        Settings s;
        std::string err;
        CHECK(applyKey(s, "BUTTON_1", "key:ctrl+0", &err), "chord accepted by the config");
        CHECK(s.buttons[1] == "key:ctrl+0", "canonical text kept");
        run(r, s, RawAxes{}, 1u, 0.5);  // held for half a second: still one reset
        CHECK(o.ev.size() == 3 && o.ev[0] == "+ctrl" && o.ev[1] == "tap:0" && o.ev[2] == "-ctrl", "ctrl down, tap 0, ctrl up (%zu events)", o.ev.size());
        o.clear();
        run(r, s, RawAxes{}, 0, 0.1);
        CHECK(o.ev.empty(), "nothing on release");
        run(r, s, RawAxes{}, 1u, 0.05);
        CHECK(o.count("tap:0") == 1, "the next press taps again");
        run(r, s, RawAxes{}, 0, 0.05);
        r.releaseAll();
        int down = 0;
        for (const auto &e : o.ev) down += e == "+ctrl" ? 1 : e == "-ctrl" ? -1 : 0;
        CHECK(down == 0, "no modifier stays pressed");
    }
    {  // order as written, only modifiers not already held, a plain key keeps its hold behavior
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        ButtonBinding b = bind(0, ButtonActionType::Key, Key::N);
        b.mods.k[0] = Key::Shift;
        b.mods.k[1] = Key::Ctrl;
        b.mods.n = 2;
        m.buttons = {b, bind(1, ButtonActionType::Key, Key::Space), bind(2, ButtonActionType::Key, Key::Ctrl)};
        r.setMode(m);
        Settings s;
        run(r, s, RawAxes{}, 1u, 0.05);
        CHECK(o.ev.size() == 5 && o.ev[0] == "+shift" && o.ev[1] == "+ctrl" && o.ev[2] == "tap:n" && o.ev[3] == "-ctrl" &&
                  o.ev[4] == "-shift",
              "modifiers in order, released in reverse");
        o.clear();
        run(r, s, RawAxes{}, 2u, 0.05);
        CHECK(o.count("+space") == 1 && o.count("tap:space") == 0, "a plain key is still held while the button is held");
        run(r, s, RawAxes{}, 0, 0.05);
        CHECK(o.count("-space") == 1, "and released with the button");
        o.clear();
        run(r, s, RawAxes{}, 4u, 0.05);       // ctrl held by a button ...
        run(r, s, RawAxes{}, 4u | 1u, 0.05);  // ... then the chord: it neither presses nor releases ctrl again
        CHECK(o.count("+ctrl") == 1 && o.count("-ctrl") == 0 && o.count("tap:n") == 1 && o.count("+shift") == 1 && o.count("-shift") == 1,
              "a held modifier stays held");
    }
    {  // a chord in a joystick mode wins like a key, and a mode switch while pressed leaves nothing down
        RunnerRec o;
        ModeRunner r(&o);
        ModeDef m = mode("t");
        m.h = act(ChannelActionType::Off);
        ButtonBinding b = bind(0, ButtonActionType::Key, Key::N0);
        b.mods.k[0] = Key::Ctrl;
        b.mods.n = 1;
        m.buttons = {b};
        r.setMode(m);
        Settings s;
        run(r, s, RawAxes{}, 1u, 0.05);
        r.setMode(mode("other"));
        run(r, s, RawAxes{}, 0, 0.05);
        int down = 0;
        for (const auto &e : o.ev) down += e == "+ctrl" ? 1 : e == "-ctrl" ? -1 : 0;
        CHECK(down == 0 && o.count("tap:0") == 1, "tapped once, nothing left pressed after a mode change");
    }
}

// Channel actions and buttons set by profiles (V_ACTION ... TWIST_ACTION, BUTTON_<n>): resolution global < device < mode <
// program, end to end through ProfileSet::resolve and the runner.
static Settings resolved(const char *conf, const char *app, const ModeDef *m = nullptr) {
    ProfileSet ps;
    const std::string text = conf;
    size_t pos = 0;
    Section *sec = &ps.sections[0];
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;
        if (line[0] == '[') {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                sec = &ps.sections[0];
            } else {
                const Section::Kind kind = line[1] == 'd' ? Section::DEVICE : Section::APP;
                const size_t i = ps.findOrAdd(kind, line.substr(colon + 1, line.size() - colon - 2));
                sec = &ps.sections[i];
            }
            continue;
        }
        const size_t eq = line.find(" = ");
        sec->kv.emplace_back(line.substr(0, eq), line.substr(eq + 3));
    }
    return ps.resolve("SpaceMouse Pro", app, m ? &m->overrides : nullptr);
}

static void testProfileActions() {
    {  // the twist does something else in one program only
        const char *conf = "[app:code]\nTWIST_ACTION = keys:left,right\n";
        const Settings code = resolved(conf, "code"), other = resolved(conf, "firefox");
        RunnerRec o;
        ModeRunner r(&o);
        run(r, code, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.wv == 0 && o.count("+ctrl") == 0 && o.count("tap:left") + o.count("tap:right") > 0, "twist sends keys in the program");
        RunnerRec o2;
        ModeRunner r2(&o2);
        run(r2, other, axis(AX_RY, 300), 0, 0.5);
        CHECK(o2.wv < 0 && o2.count("+ctrl") == 1, "other programs still zoom");
    }
    {  // tilt cleared, push as before (the split key of the profile)
        const Settings s = resolved("[app:code]\nV_TILT_ACTION = none\n", "code");
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_RX, 300), 0, 0.5);
        CHECK(o.wv == 0, "vertical tilt cleared");
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.wv > 0, "vertical push still scrolls");
        RunnerRec o2;
        ModeRunner r2(&o2);
        const Settings s2 = resolved("[app:code]\nV_PUSH_ACTION = none\n", "code");
        run(r2, s2, axis(AX_Z, 300), 0, 0.5);
        CHECK(o2.wv == 0, "vertical push cleared");
        run(r2, s2, axis(AX_RX, 300), 0, 0.5);
        CHECK(o2.wv > 0, "vertical tilt still scrolls");
    }
    {  // precedence: [global] under the mode, the program over it
        ModeDef m = mode("timeline");
        m.h = act(ChannelActionType::Keys, Key::Left, Key::Right);
        const char *conf = "[global]\nH_ACTION = none\nTWIST_ACTION = none\n[app:code]\nH_ACTION = wheel_h\n";
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(m);
        const Settings other = resolved(conf, "firefox", &m);
        run(r, other, axis(AX_X, 300), 0, 0.3);
        CHECK(o.count("tap:right") > 0 && o.wh == 0, "the mode beats [global]");
        run(r, other, axis(AX_RY, 300), 0, 0.3);
        CHECK(o.wv == 0, "[global] TWIST_ACTION = none applies while the mode says nothing about the twist");
        RunnerRec o2;
        ModeRunner r2(&o2);
        r2.setMode(m);
        run(r2, resolved(conf, "code", &m), axis(AX_X, 300), 0, 0.3);
        CHECK(o2.wh > 0 && o2.count("tap:right") == 0, "the program beats the mode");
    }
    {  // a channel key of the program drops the split of the mode
        ModeDef m = mode("split");
        m.vSplit.tilt = act(ChannelActionType::Off);
        m.vSplit.tiltSet = true;
        RunnerRec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, resolved("[app:code]\nV_ACTION = wheel_v\n", "code", &m), axis(AX_RX, 300), 0, 0.3);
        CHECK(o.wv > 0, "V_ACTION of the program makes the whole channel scroll again");
    }
    {  // buttons: the mode beats [global] and [device:*], the program beats the mode; none unbinds
        ModeDef m = mode("b");
        m.buttons = {bind(0, ButtonActionType::Key, Key::Space), bind(1, ButtonActionType::Click)};
        const char *conf = "[global]\nBUTTON_1 = rightclick\n[app:code]\nBUTTON_2 = none\n[app:vim]\nBUTTON_1 = key:enter\n";
        {
            RunnerRec o;
            ModeRunner r(&o);
            r.setMode(m);
            run(r, resolved(conf, "firefox", &m), RawAxes{}, 3u, 0.05);
            CHECK(o.count("+space") == 1 && o.count("+m1") == 1 && o.count("+m3") == 0, "the mode beats the Global binding");
        }
        {
            RunnerRec o;
            ModeRunner r(&o);
            r.setMode(m);
            run(r, resolved(conf, "code", &m), RawAxes{}, 3u, 0.05);
            CHECK(o.count("+space") == 1 && o.count("+m1") == 0, "BUTTON_2 = none in the program unbinds the mode's click");
        }
        {
            RunnerRec o;
            ModeRunner r(&o);
            r.setMode(m);
            run(r, resolved(conf, "vim", &m), RawAxes{}, 3u, 0.05);
            CHECK(o.count("+space") == 0 && o.count("+enter") == 1 && o.count("+m1") == 1, "the program's binding beats the mode's");
        }
    }
    {  // nothing set: exactly the mode, as before
        const Settings s = resolved("[global]\nSCROLL_V_SENS = 5\n", "code");
        CHECK(!s.actions[0].any() && !s.actions[1].any() && s.app_buttons.empty(), "no profile actions");
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(r.legacyPath() && o.count("+ctrl") == 1, "default path");
    }
}

// A profile that switches the zoom channel off (ZOOM_SENS = 0): no wheel, no zoom key, and a zoom that is
// running is ended with the held key released (reload while the twist is held).
static void testZoomChannelOff() {
    Settings s;
    Settings off = s;
    off.zoom.sensitivity = 0;
    for (int custom = 0; custom < 2; ++custom) {
        RunnerRec o;
        ModeRunner r(&o);
        if (custom) {
            ModeDef m = mode("t");
            m.v = act(ChannelActionType::WheelV);
            r.setMode(m);
        }
        run(r, off, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.wv == 0 && o.count("+ctrl") == 0, "zoom off: no wheel and no Ctrl (custom=%d)", custom);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 1 && o.wv < 0, "zoom on again (custom=%d)", custom);
        o.clear();
        run(r, off, axis(AX_RY, 300), 0, 0.2);  // the profile changes while the twist is held
        CHECK(o.count("-ctrl") == 1 && o.wv == 0, "a running zoom ends and Ctrl is released (custom=%d)", custom);
        o.clear();
        run(r, off, axis(AX_Z, 300), 0, 0.3);  // everything else keeps working
        CHECK(o.wv > 0, "scrolling is not affected (custom=%d)", custom);
    }
}

// "Tilt forward = zoom" in one program: the tilt part zooms (modifier + wheel) on its own axes, the push keeps scrolling without the
// modifier, and while the tilt zooms the other wheel motions pause (they would zoom too).
static void testTiltZoom() {
    const Settings s = resolved("[app:code]\nV_TILT_ACTION = zoom\n", "code");
    {
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_RX, 300), 0, 0.5);
        CHECK(o.count("+ctrl") == 1 && o.wv != 0, "tilt zooms: modifier and wheel");
        run(r, s, RawAxes{}, 0, 0.2);
        CHECK(o.count("-ctrl") == 1, "the modifier is released at rest");
    }
    {
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_Z, 300), 0, 0.5);
        CHECK(o.wv > 0 && o.count("+ctrl") == 0, "the push still scrolls, without the modifier");
    }
    {
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_X, 300), 0, 0.5);
        CHECK(o.wh > 0, "sideways push scrolls alone");
        RawAxes both = axis(AX_RX, 300);
        both[AX_X] = 300;
        o.clear();
        run(r, s, both, 0, 0.5);
        CHECK(o.wv != 0 && o.wh == 0 && o.count("+ctrl") == 1, "while the tilt zooms the sideways scroll pauses");
    }
    {  // the twist keeps zooming by its latch next to a tilt zoom, and the tilt of the other direction is not a zoom
        RunnerRec o;
        ModeRunner r(&o);
        run(r, s, axis(AX_RY, 300), 0, 0.5);
        CHECK(o.wv < 0 && o.count("+ctrl") == 1, "the twist still zooms in");
    }
}

int main() {
    testTiltZoom();
    testProfileActions();
    testZoomChannelOff();
    testChordButtons();
    testSettingsButtons();
    testModeSwitchRequest();
    testToggleNav();
    testToggleNavFromSettings();
    testToggleModeRequest();
    testLegacyBindings();
    testLegacyNavAndClick();
    testCustomChannels();
    testChordChannels();
    testHoldMode();
    testStepChannels();
    testReleases();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_mode_runner: OK");
    return 0;
}
