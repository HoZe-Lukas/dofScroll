// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The pointer as a motion function (stage 5 of docs/plan-motion-functions.md): `pointer_x` and `pointer_y` turn the value `u` of
// one motion (steps per second, signed, after smoothing, dead zone, curve and SENS) into relative pointer movement. One step is
// MOUSE_SPEED pixels, so the pointer moves u * MOUSE_SPEED pixels per second. The fraction of a pixel that does not fit into
// this tick is kept for the next one while the motion is deflected and dropped at rest (nothing is left over after a release).
//
// Rate cap, done here by the caller of Output::moveMouse: at most kMaxMouseStep (127) pixels per call and axis, one call per tick.
// At the default TICK_HZ of 250 that is 31750 pixels per second and axis, far above any screen; what exceeds the cap in one tick
// is dropped, not carried to the next tick, so the pointer never keeps moving after the cap was reached.
#pragma once
#include <cmath>

#include "output.hpp"

namespace spn {

class PointerMover {
public:
    // Signed whole pixels of this tick, |n| <= kMaxMouseStep. u: steps per second (positive: right resp. down), dt: seconds,
    // speed: MOUSE_SPEED (pixels per step).
    int step(double u, double dt, double speed) {
        if (u == 0.0 || !std::isfinite(u) || !std::isfinite(speed) || !std::isfinite(dt)) {
            reset();
            return 0;
        }
        // The product is kept in its own variable, so that it cannot be fused with the sum (fused multiply-add on arm64 gave
        // other roundings than x86-64); the result is the same on every platform (as in key_engine.hpp).
        volatile double pxPerSecond = u * speed;
        volatile double add = pxPerSecond * dt;
        if (!std::isfinite(add)) {
            reset();
            return 0;
        }
        acc_ += add;
        const double whole = std::trunc(acc_);  // toward 0: the remainder keeps the sign
        acc_ -= whole;
        if (whole > kMaxMouseStep || whole < -kMaxMouseStep) acc_ = 0.0;  // over the cap: the excess is dropped
        return whole > kMaxMouseStep ? kMaxMouseStep : whole < -kMaxMouseStep ? -kMaxMouseStep : static_cast<int>(whole);
    }
    void reset() { acc_ = 0.0; }

private:
    double acc_ = 0.0;
};

}  // namespace spn
