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

int main() {
    testSettingsButtons();
    testModeSwitchRequest();
    testLegacyBindings();
    testLegacyNavAndClick();
    testCustomChannels();
    testReleases();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_mode_runner: OK");
    return 0;
}
