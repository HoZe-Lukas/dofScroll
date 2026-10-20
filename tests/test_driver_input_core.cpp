// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// DriverInputCore: unit conversion and event generation of the driver-interface backends.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "platform/driver_input_core.hpp"

using namespace spn;

static std::vector<InputEvent> drain(DriverInputCore &c) {
    std::vector<InputEvent> v;
    InputEvent e;
    while (c.wait(&e, 0) == WaitResult::Event) v.push_back(e);
    return v;
}

int main() {
    // ---- axis/angle to a rotation vector ----
    {
        auto v = axisAngleToVector(1, 0, 0, -25);
        assert(v[0] == -25 && v[1] == 0 && v[2] == 0);
        v = axisAngleToVector(0, 0.6, 0.8, 10);
        assert(std::fabs(v[1] - 6) < 1e-9 && std::fabs(v[2] - 8) < 1e-9);
        v = axisAngleToVector(NAN, 0, 0, 1);
        assert(v[0] == 0 && v[1] == 0 && v[2] == 0);
        v = axisAngleToVector(1, 1, 1, INFINITY);
        assert(v[0] == 0);
    }

    DriverInputCore c;
    c.open();
    // nothing while no device is present
    c.onAxes({1, 2, 3, 4, 5, 6}, 1, 1);
    c.onButtons(1);
    assert(drain(c).empty() && c.devices().empty() && !c.present());

    // ---- device added: fallback name, factory buttons capped at 32 ----
    c.deviceAdded("", 0, 0, 0);
    auto ev = drain(c);
    assert(ev.size() == 1 && ev[0].type == InputEventType::DevicesChanged);
    auto d = c.devices();
    assert(d.size() == 1 && d[0].id == 1 && d[0].name == "SpaceMouse (driver)" && d[0].axes == 6 && d[0].reportedButtons == 0);
    c.deviceAdded("SpaceMouse Pro", 0x46d, 0xc62b, 15);
    drain(c);
    d = c.devices();
    assert(d[0].name == "SpaceMouse Pro" && d[0].vendor == 0x46d && d[0].product == 0xc62b && d[0].reportedButtons == 15);
    c.deviceAdded("X", 1, 2, 99);
    drain(c);
    assert(c.devices()[0].reportedButtons == 32);

    // ---- axes: scaling, rounding, clamping, only changes ----
    c.onAxes({10, -20, 30, 40, -50, 60}, 1.0, 1.0);
    ev = drain(c);
    assert(ev.size() == 1 && ev[0].type == InputEventType::Motion && ev[0].device == 1);
    assert(ev[0].axes == (std::array<int, 6>{10, -20, 30, 40, -50, 60}));
    c.onAxes({10, -20, 30, 40, -50, 60}, 1.0, 1.0);  // unchanged: nothing
    assert(drain(c).empty());
    c.onAxes({1.5, -1.5, 0.4, 100, 100, 100}, 2.0, 0.5);  // translation x2, rotation x0.5
    ev = drain(c);
    assert(ev.size() == 1 && ev[0].axes == (std::array<int, 6>{3, -3, 1, 50, 50, 50}));
    c.onAxes({1e9, -1e9, NAN, INFINITY, 0, 0}, 1.0, 1.0);
    ev = drain(c);
    assert(ev.size() == 1 && ev[0].axes[0] == kHidAxisLimit && ev[0].axes[1] == -kHidAxisLimit && ev[0].axes[2] == 0 &&
           ev[0].axes[3] == 0);

    // ---- buttons: one event per changed bit, lowest first ----
    c.onButtons(0x5);
    ev = drain(c);
    assert(ev.size() == 2 && ev[0].button == 0 && ev[0].down && ev[1].button == 2 && ev[1].down);
    c.onButtons(0x5);
    assert(drain(c).empty());
    c.onButtons(0x4);
    ev = drain(c);
    assert(ev.size() == 1 && ev[0].button == 0 && !ev[0].down);
    c.onButtons(0x80000000u);
    ev = drain(c);
    assert(ev.size() == 2 && ev[0].button == 2 && !ev[0].down && ev[1].button == 31 && ev[1].down);

    // ---- reset: neutral state without removing the device ----
    c.onAxes({5, 0, 0, 0, 0, 0}, 1, 1);
    drain(c);
    c.reset();
    ev = drain(c);
    assert(ev.size() == 2 && ev[0].type == InputEventType::Button && ev[0].button == 31 && !ev[0].down &&
           ev[1].type == InputEventType::Motion && ev[1].axes == (std::array<int, 6>{}));
    assert(c.present());
    c.reset();
    assert(drain(c).empty());

    // ---- removal: neutral state, then the device change; a second removal is a no-op ----
    c.onAxes({7, 0, 0, 0, 0, 0}, 1, 1);
    c.onButtons(0x2);
    drain(c);
    c.deviceRemoved();
    ev = drain(c);
    assert(ev.size() == 3 && ev[0].type == InputEventType::Button && ev[0].button == 1 && !ev[0].down &&
           ev[1].type == InputEventType::Motion && ev[2].type == InputEventType::DevicesChanged);
    assert(c.devices().empty());
    c.deviceRemoved();
    assert(drain(c).empty());

    // ---- a returning device starts from the neutral state ----
    c.deviceAdded("", 0, 0, 0);
    drain(c);
    c.onAxes({0, 0, 0, 0, 0, 0}, 1, 1);
    assert(drain(c).empty());  // the old state was cleared: zero is not a change

    // ---- close / reopen ----
    c.close();
    InputEvent e;
    assert(c.wait(&e, 0) == WaitResult::Closed);
    c.open();
    assert(c.wait(&e, 0) == WaitResult::Timeout && !c.present());

    std::puts("test_driver_input_core: ok");
    return 0;
}
