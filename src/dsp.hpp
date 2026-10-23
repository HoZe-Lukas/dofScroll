// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Per-channel signal processing (scroll V, scroll H, zoom): mix -> sign ->
// smoothing -> deadzone -> curve -> speed. Pure computation, without X11/libspnav,
// so that it is unit-testable.
#pragma once
#include "common.hpp"
#include "config.hpp"

namespace spn {

// Sum of the terms (double, still without sign/deadzone).
double evalMix(const Mix &m, const RawAxes &raw);

// Subtract the deadzone: |v| <= dz -> 0, otherwise magnitude reduced by dz, sign kept.
double applyDeadzone(double v, int dz);

// Apply the curve to a deadzone-adjusted value (sign is kept).
// Without curve_points: sign(v) * (|v|/CURVE_REF)^exp * CURVE_REF  (exp == 1 -> linear).
// With curve_points: |v| linear * interpolated factor.
constexpr double CURVE_REF = 200.0;
double applyCurve(const ChannelCfg &c, double v);

// Time-based first-order low-pass filter, independent of the tick rate:
// alpha = 1 - exp(-dt / tau). tau_ms <= 0 -> pass through. On reset (e.g.
// connection loss) the filter starts at 0.
class Smoother {
public:
    void reset() { y_ = 0.0; }
    double step(double in, double dt_s, double tau_ms);
private:
    double y_ = 0.0;
};

// A channel instance with filter state. process() returns the speed in
// "steps per second" (signed), calibrated like the previous behavior:
// (curved * sensitivity / 2500) * 125. sensitivity == 0 -> 0.
class Channel {
public:
    double process(const ChannelCfg &c, const RawAxes &raw, double dt_s);
    // Like process(), but with an already computed raw value (e.g. zoom, where the
    // mix is needed beforehand for the dominance check).
    double processValue(const ChannelCfg &c, double mixed, double dt_s);
    void reset() { smoother_.reset(); }
private:
    Smoother smoother_;
};

// Step accumulator: collects steps/s * dt and emits whole steps.
class StepAccumulator {
public:
    // Adds rate*dt; returns whole steps (signed), the remainder is kept.
    int add(double steps_per_s, double dt_s);
    void reset() { acc_ = 0.0; }
private:
    double acc_ = 0.0;
};

// Zoom detection (latch with hysteresis + entry delay).
// twist = |zoom mix after sign|, lateral = strongest single axis from
// settings.zoom_lateral, deadzone = zoom.deadzone.
class ZoomDetector {
public:
    // true while zooming.
    bool update(double twist_abs, double lateral_abs, const Settings &s, double dt_s);
    void reset() { latched_ = false; candidate_s_ = 0.0; }
    bool active() const { return latched_; }
private:
    bool latched_ = false;
    double candidate_s_ = 0.0;
};

// Strongest single axis (magnitude) from the list.
// Axes that are part of the zoom mix itself are skipped: the twist cannot dominate against itself
// (a mapping like ZOOM_MIX = -rz with rz still in ZOOM_LATERAL would never zoom).
double lateralMax(const std::vector<Axis> &axes, const RawAxes &raw, const Mix &zoom_mix);
double lateralMax(const std::vector<Axis> &axes, const RawAxes &raw);

}  // namespace spn
