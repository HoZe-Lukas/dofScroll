// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Curve preview of the settings page: the response of one channel for raw input magnitudes
// 0..maxInput, computed with the daemon's own functions from dsp.hpp (deadzone, curve, speed
// calibration), so the picture always matches the real behavior. Smoothing and sign are not
// part of the static curve.
#pragma once
#include <vector>

#include "config.hpp"
#include "dsp.hpp"

namespace spn::ui {

struct CurvePoint {
    double input;   // raw sensor units (magnitude, before the deadzone)
    double curved;  // after deadzone and curve (applyDeadzone + applyCurve), >= 0
    double speed;   // steps per second, same calibration as Channel::processValue
};

// samples >= 2 points, evenly spaced from 0 to maxInput (> 0). Invalid arguments: empty list.
inline std::vector<CurvePoint> computeCurvePreview(const ChannelCfg &c, int samples = 64,
                                                   double maxInput = 350.0) {
    std::vector<CurvePoint> out;
    if (samples < 2 || !(maxInput > 0)) return out;
    for (int i = 0; i < samples; ++i) {
        const double x = maxInput * i / (samples - 1);
        const double v = applyCurve(c, applyDeadzone(x, c.deadzone));
        out.push_back({x, v, c.sensitivity == 0 ? 0.0 : (v * c.sensitivity / 2500.0) * 125.0});
    }
    return out;
}

// Highest speed of the preview (for the axis scale); 0 for an empty list.
inline double previewMaxSpeed(const std::vector<CurvePoint> &p) {
    double m = 0;
    for (const auto &q : p) m = q.speed > m ? q.speed : m;
    return m;
}

}  // namespace spn::ui
