// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Signal processing, see dsp.hpp.
#include "dsp.hpp"

#include <algorithm>
#include <cmath>

namespace spn {

double evalMix(const Mix &m, const RawAxes &raw) {
    double sum = 0.0;
    for (const Term &t : m.terms) sum += t.weight * raw[t.axis];
    return sum;
}

double applyDeadzone(double v, int dz) {
    double a = std::fabs(v);
    if (a <= dz) return 0.0;
    double r = a - dz;
    return v < 0 ? -r : r;
}

double applyCurve(const ChannelCfg &c, double v) {
    if (v == 0.0) return 0.0;
    double a = std::fabs(v);
    double out;
    const auto &p = c.curve_points;
    if (!p.empty()) {
        double f;
        if (a <= p.front().first) {
            f = p.front().second;
        } else if (a >= p.back().first) {
            f = p.back().second;
        } else {
            f = p.back().second;
            for (size_t i = 1; i < p.size(); ++i) {
                if (a <= p[i].first) {
                    double x0 = p[i - 1].first, x1 = p[i].first;
                    double y0 = p[i - 1].second, y1 = p[i].second;
                    f = (x1 > x0) ? y0 + (y1 - y0) * (a - x0) / (x1 - x0) : y1;
                    break;
                }
            }
        }
        out = a * f;
    } else if (c.curve_exp == 1.0) {
        out = a;
    } else {
        out = std::pow(a / CURVE_REF, c.curve_exp) * CURVE_REF;
    }
    return v < 0 ? -out : out;
}

double Smoother::step(double in, double dt_s, double tau_ms) {
    if (tau_ms <= 0.0) {
        y_ = in;
        return in;
    }
    double alpha = 1.0 - std::exp(-dt_s / (tau_ms / 1000.0));
    y_ += alpha * (in - y_);
    return y_;
}

double Channel::processValue(const ChannelCfg &c, double mixed, double dt_s) {
    double v = mixed * c.sign;
    v = smoother_.step(v, dt_s, c.smooth_ms);
    if (c.sensitivity == 0) return 0.0;
    v = applyDeadzone(v, c.deadzone);
    v = applyCurve(c, v);
    double rate = v * c.sensitivity / 2500.0 * 125.0;
    // Guard against inf/NaN and absurd rates (e.g. from extreme curve values);
    // the linear maximum is 350*255/2500*125 = ~4460 steps/s.
    if (!std::isfinite(rate)) return 0.0;
    constexpr double MAX_RATE = 20000.0;
    return std::max(-MAX_RATE, std::min(MAX_RATE, rate));
}

double Channel::process(const ChannelCfg &c, const RawAxes &raw, double dt_s) {
    return processValue(c, evalMix(c.mix, raw), dt_s);
}

int StepAccumulator::add(double steps_per_s, double dt_s) {
    double add = steps_per_s * dt_s;
    if (!std::isfinite(add)) { acc_ = 0.0; return 0; }
    acc_ += add;
    double whole = std::trunc(acc_);  // toward 0: the remainder keeps the sign
    acc_ -= whole;
    // Upper limit per call: never more than an absurd flood of clicks per tick.
    constexpr double MAX_STEPS_PER_CALL = 200.0;
    return static_cast<int>(std::max(-MAX_STEPS_PER_CALL, std::min(MAX_STEPS_PER_CALL, whole)));
}

bool ZoomDetector::update(double twist_abs, double lateral_abs, const Settings &s,
                          double dt_s) {
    const double dz = s.zoom.deadzone;
    if (latched_) {
        if (twist_abs <= dz || lateral_abs > twist_abs * s.zoom_exit_ratio) latched_ = false;
        candidate_s_ = 0.0;
        return latched_;
    }
    if (twist_abs > dz && twist_abs > lateral_abs) {
        candidate_s_ += dt_s;
        if (candidate_s_ * 1000.0 >= s.zoom_enter_ms - 1e-6) {
            latched_ = true;
            candidate_s_ = 0.0;
        }
    } else {
        candidate_s_ = 0.0;
    }
    return latched_;
}

double lateralMax(const std::vector<Axis> &axes, const RawAxes &raw) {
    double m = 0.0;
    for (Axis a : axes) m = std::max(m, std::fabs(static_cast<double>(raw[a])));
    return m;
}

}  // namespace spn
