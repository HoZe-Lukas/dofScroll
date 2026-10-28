// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The key engine (stage 3 of docs/plan-motion-functions.md): what turns the value `u` of one motion (steps per second, signed,
// after smoothing, dead zone, curve and SENS) into key presses. Two functions, one per motion and tick:
//
//   KeyRepeater     analog (`keys:<neg>,<pos>`): the key repeats at clamp(|u|, rate_min, rate_max) presses per second, never
//                   faster than the hard cap min(100, tick_hz / 2); at most kMaxPressesPerFunction presses per tick. The
//                   first press comes after 1/rate (as it always did), or at once when the motion leaves rest if a lower
//                   rate limit or a trigger is set (the accumulator is preloaded). With a trigger the motion is "at rest"
//                   below it, and it rests again at half of it.
//   DigitalTrigger  digital (`step:<neg>,<pos>`): one press when |u| reaches the trigger on a side while that side is
//                   armed, re-armed when |u| has fallen to half the trigger; the opposite side is another key and re-arms on
//                   its own. The same state drives hold:<neg>,<pos> (stage 4): the key, chord or mouse button of the side is held
//                   from the press until the re-arm at half the trigger.
//
// With the default parameters (no limit, no trigger set) both reproduce what ModeRunner did before this stage (the golden
// master tests/test_golden_motions.cpp is the proof), except that the press count per tick is capped at 4 (it was 200).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.hpp"

namespace spn {

// The parameters of one motion's key function, resolved from Settings (KeyTuning) and the tick rate.
struct KeyParams {
    double rate_min = 0.0;  // 0 = no lower limit
    double rate_max = 0.0;  // 0 = no upper limit below the hard cap
    double trigger = 0.0;   // 0 = not set (analog) / the caller passes STEP_TRIGGER (digital)
    double hard_cap = 100.0;
};

inline KeyParams keyParamsOf(const Settings &s, int keyMotion) {
    KeyParams p;
    const KeyTuning &t = s.key_tuning[keyMotion];
    if (t.set & KeyTuning::F_RATE_MIN) p.rate_min = t.rate_min;
    if (t.set & KeyTuning::F_RATE_MAX) p.rate_max = t.rate_max;
    if (t.set & KeyTuning::F_TRIGGER) p.trigger = t.trigger;
    p.hard_cap = std::min(100.0, s.tick_hz / 2.0);
    return p;
}

class KeyRepeater {
public:
    // Signed number of presses of this tick (positive: the positive side), |n| <= kMaxPressesPerFunction.
    int step(double u, double dt, const KeyParams &p) {
        const double mag = std::fabs(u);
        const bool trigger = p.trigger > 0.0;
        if (!std::isfinite(u) || u == 0.0) return rest();
        if (trigger) {
            if (!active_ && mag < p.trigger) return rest();
            if (active_ && mag <= p.trigger / 2.0) return rest();
        }
        const double sgn = u < 0.0 ? -1.0 : 1.0;
        if (!active_) {
            active_ = true;
            if (p.rate_min > 0.0 || trigger) acc_ = sgn;  // the first press at once
        }
        double rate = std::min(mag, p.hard_cap);
        if (p.rate_max > 0.0) rate = std::min(rate, p.rate_max);
        if (p.rate_min > 0.0) rate = std::max(rate, std::min(p.rate_min, p.rate_max > 0.0 ? p.rate_max : p.hard_cap));
        rate = std::min(rate, p.hard_cap);
        // Same two statements as StepAccumulator::add: a product kept in its own variable cannot be fused with the sum
        // (fused multiply-add on arm64 gave other roundings), so the result is the same on every platform.
        volatile double signedRate = sgn * rate;
        volatile double add = signedRate * dt;
        acc_ += add;
        const double whole = std::trunc(acc_);  // toward 0: the remainder keeps the sign
        acc_ -= whole;
        return static_cast<int>(std::max(-double(kMaxPressesPerFunction), std::min(double(kMaxPressesPerFunction), whole)));
    }
    void reset() {
        acc_ = 0.0;
        active_ = false;
    }

private:
    int rest() {
        reset();
        return 0;
    }
    double acc_ = 0.0;
    bool active_ = false;
};

class DigitalTrigger {
public:
    // -1, 0 or +1: one press on the side of the deflection when the trigger is reached while armed.
    int step(double u, double trigger) {
        if (state_ > 0 && u <= trigger / 2.0) state_ = 0;
        if (state_ < 0 && u >= -trigger / 2.0) state_ = 0;
        if (state_ == 0) {
            if (u >= trigger) { state_ = 1; return 1; }
            if (u <= -trigger) { state_ = -1; return -1; }
        }
        return 0;
    }
    void reset() { state_ = 0; }
    // 0 armed (rest), +1 / -1 deflected beyond the trigger on that side since the last press, not yet back at half of it.
    // hold:<neg>,<pos> holds its key or button exactly while this is not 0.
    int state() const { return state_; }

private:
    int8_t state_ = 0;  // 0 armed, +1 / -1 fired on that side
};

}  // namespace spn
