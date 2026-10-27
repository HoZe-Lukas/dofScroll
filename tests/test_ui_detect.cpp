// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Detect by moving" (issue #143): the strongest axis selects its motion, crosstalk and ties do not, one
// selection per movement, buttons on their rising edge.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "ui_fixture.hpp"
#include "ui/model/detect_model.hpp"

using namespace spn;
using namespace spn::ui;

static RawAxes axes(int x, int y, int z, int rx, int ry, int rz) {
    RawAxes a{};
    a[AX_X] = x;
    a[AX_Y] = y;
    a[AX_Z] = z;
    a[AX_RX] = rx;
    a[AX_RY] = ry;
    a[AX_RZ] = rz;
    return a;
}
static const RawAxes kRest = axes(0, 0, 0, 0, 0, 0);

// Feeds the same sample n times and returns the first event that is not None.
static DetectEvent feedN(DetectModel &d, const RawAxes &a, int n, uint32_t buttons = 0) {
    DetectEvent first;
    for (int i = 0; i < n; ++i) {
        const DetectEvent e = d.feed(a, buttons);
        if (first.kind == DetectEvent::None && e.kind != DetectEvent::None) first = e;
    }
    return first;
}

// the motion of an axis in the configuration of the fixture (the Linux order of the built-in channels)
static Motion motionOf(Axis a) {
    Motion m = Motion::VPush;
    deriveAxisRoles(Settings()).motionOf(a, &m);
    return m;
}

int main() {
    useFreshDefaultOf("linux");  // not the host system
    // every axis selects its motion, with the direction
    const struct { RawAxes a; Motion m; int sign; } cases[] = {
        {axes(0, 0, 300, 0, 0, 0), Motion::VPush, 1},   {axes(0, 0, 0, -300, 0, 0), Motion::VTilt, -1},
        {axes(300, 0, 0, 0, 0, 0), Motion::HPush, 1},   {axes(0, 0, 0, 0, 0, 300), Motion::HTilt, 1},
        {axes(0, 0, 0, 0, -300, 0), Motion::Twist, -1}, {axes(0, 300, 0, 0, 0, 0), Motion::Lift, 1},
    };
    for (const auto &c : cases) {
        DetectModel d;
        const DetectEvent e = feedN(d, c.a, 6);
        assert(e.kind == DetectEvent::Motion && motionOf(e.axis) == c.m && e.sign == c.sign);
    }

    // crosstalk: a push sideways with y 293 next to x 350 still selects x (the strongest wins)
    {
        DetectModel d;
        const DetectEvent e = feedN(d, axes(350, 293, 0, 0, 0, 0), 6);
        assert(e.kind == DetectEvent::Motion && motionOf(e.axis) == Motion::HPush);
    }
    // a tie is ambiguous: nothing is selected until it is cleaner
    {
        DetectModel d;
        assert(feedN(d, axes(300, 0, 295, 0, 0, 0), 8).kind == DetectEvent::None);
        const DetectEvent e = feedN(d, axes(0, 0, 400, 0, 0, 0), 12);
        assert(e.kind == DetectEvent::Motion && motionOf(e.axis) == Motion::VPush);
    }
    // too weak: nothing
    {
        DetectModel d;
        assert(feedN(d, axes(0, 0, 60, 0, 0, 0), 8).kind == DetectEvent::None);
    }
    // a single spike does not count
    {
        DetectModel d;
        assert(d.feed(axes(0, 0, 500, 0, 0, 0), 0).kind == DetectEvent::None);
        assert(feedN(d, kRest, 8).kind == DetectEvent::None);
    }
    // one selection per movement: holding the cap reports once, and after rest the next movement reports again
    {
        DetectModel d;
        int n = 0;
        for (int i = 0; i < 30; ++i)
            if (d.feed(axes(0, 0, 300, 0, 0, 0), 0).kind == DetectEvent::Motion) ++n;
        assert(n == 1);
        for (int i = 0; i < DetectModel::kRestSamples; ++i) d.feed(kRest, 0);
        const DetectEvent e = feedN(d, axes(300, 0, 0, 0, 0, 0), 6);
        assert(e.kind == DetectEvent::Motion && motionOf(e.axis) == Motion::HPush);
    }
    // moving on without rest does not select a second motion
    {
        DetectModel d;
        assert(feedN(d, axes(0, 0, 300, 0, 0, 0), 6).kind == DetectEvent::Motion);
        assert(feedN(d, axes(300, 0, 0, 0, 0, 0), 12).kind == DetectEvent::None);
    }
    // buttons: rising edge, lowest first, already pressed ones at the start are ignored
    {
        DetectModel d;
        assert(d.feed(kRest, 0x2).kind == DetectEvent::None);  // button 2 was already down when Detect started
        assert(d.feed(kRest, 0x2).kind == DetectEvent::None);
        DetectEvent e = d.feed(kRest, 0x2 | 0x4);
        assert(e.kind == DetectEvent::Button && e.button == 3);
        assert(d.feed(kRest, 0x2 | 0x4).kind == DetectEvent::None);  // held
        assert(d.feed(kRest, 0).kind == DetectEvent::None);
        e = d.feed(kRest, 0x1 | 0x80000000u);
        assert(e.kind == DetectEvent::Button && e.button == 1);
        e = d.feed(kRest, 0x80000000u);
        assert(e.kind == DetectEvent::None);
        d.reset();
        assert(d.feed(kRest, 0x80000000u).kind == DetectEvent::None);  // seeded again after a reset
    }
    std::puts("test_ui_detect OK");
    return 0;
}
