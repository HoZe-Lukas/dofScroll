// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Evaluation logic of the "learn axes" wizard (tray, "Axes" tab).
//
// Header-only and deliberately free of GTK/libspnav so that it can be tested in
// tests/test_axis_wizard.cpp without a GUI and without hardware.
//
// Principle: in each step the user moves the cap for ~3 s in an announced
// direction (first in the stated direction, then back). The peak value
// (magnitude) per raw axis is measured. Because the SpaceMouse can show strong
// crosstalk (example SpaceMouse Pro, "push sideways": x 350, y 293 ...), the
// ABSOLUTELY strongest axis is always chosen - after smoothing (median over 3
// samples, a single spike does not count); nearly equally strong axes (TIE_RATIO)
// count as ambiguous. The sign comes from the first sustained half-movement
// (at least MIN_RUN samples above SIGN_THRESHOLD) - it points in the announced
// direction, which thereby becomes positive.
#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"

namespace spn {
namespace wiz {

// Order = order of the steps in the wizard.
enum Role { ROLE_PUSH_V = 0, ROLE_TILT_V, ROLE_PUSH_H, ROLE_TILT_H, ROLE_TWIST, ROLE_COUNT };

constexpr int SIGN_THRESHOLD = 40;      // first half-movement: the sign counts from this magnitude
constexpr int MIN_PEAK = 80;            // below this: "too weak"
constexpr double AMBIGUOUS_RATIO = 0.85;  // second-strongest/strongest above this -> "close call" hint
constexpr double TIE_RATIO = 0.95;      // above this: practically a tie -> ambiguous, invalid

inline const char *roleTitle(int r) {
    static const char *t[ROLE_COUNT] = {"Push forward/back", "Tilt forward/back",
                                        "Push sideways", "Tilt sideways",
                                        "Twist"};
    return (r >= 0 && r < ROLE_COUNT) ? t[r] : "?";
}

// Channel of a role: 0 = vertical scroll, 1 = horizontal scroll, 2 = zoom.
inline int roleChannel(int r) {
    return r == ROLE_PUSH_V || r == ROLE_TILT_V ? 0 : (r == ROLE_PUSH_H || r == ROLE_TILT_H ? 1 : 2);
}

inline bool roleOptional(int r) { return r == ROLE_TILT_V || r == ROLE_TILT_H; }

struct StepResult {
    bool valid = false;        // axis detected and strong enough
    bool too_weak = false;     // peak < min_peak
    bool ambiguous = false;    // two axes (nearly) equally strong -> invalid
    int axis = -1;             // spn::Axis of the strongest axis, -1 = no data
    int sign = 0;              // +1/-1: sign of the announced direction
    int peak = 0;              // smoothed peak magnitude of the chosen axis
    std::array<int, AX_COUNT> peaks{};  // smoothed peak magnitude per axis
    int runner_up = -1;        // second-strongest axis
    double ratio = 0.0;        // peaks[runner_up] / peak
    std::string message;       // plain text for display
};

// Peak magnitude per axis over all samples.
inline std::array<int, AX_COUNT> peakAbs(const std::vector<RawAxes> &samples) {
    std::array<int, AX_COUNT> p{};
    for (const auto &s : samples)
        for (int a = 0; a < AX_COUNT; ++a) {
            int v = std::abs(s[a]);
            if (v > p[a]) p[a] = v;
        }
    return p;
}

// Index of the axis with the largest magnitude (the first on a tie), -1 if all are 0.
inline int strongestAxis(const std::array<int, AX_COUNT> &peaks, int exclude = -1) {
    int best = -1;
    for (int a = 0; a < AX_COUNT; ++a) {
        if (a == exclude) continue;
        if (best < 0 || peaks[a] > peaks[best]) best = a;
    }
    if (best >= 0 && peaks[best] == 0 && exclude < 0) return -1;
    return best;
}

// Smoothed peak magnitude per axis: maximum of the median over each
// SMOOTH_WIN consecutive magnitudes. A single outlier (spike, crosstalk)
// therefore does not count; the peak must be held for at least 2 of 3 samples.
// With fewer than SMOOTH_WIN samples a window over all available ones is used
// (median = lower value). center[a] = index of the window center of the peak.
constexpr int SMOOTH_WIN = 3;

inline std::array<int, AX_COUNT> smoothedPeaks(const std::vector<RawAxes> &samples,
                                               std::array<int, AX_COUNT> *center = nullptr) {
    std::array<int, AX_COUNT> p{};
    std::array<int, AX_COUNT> c{};
    const int n = int(samples.size());
    const int w = n < SMOOTH_WIN ? n : SMOOTH_WIN;
    for (int a = 0; a < AX_COUNT && w > 0; ++a)
        for (int i = 0; i + w <= n; ++i) {
            std::array<int, SMOOTH_WIN> win{};
            for (int k = 0; k < w; ++k) win[k] = std::abs(samples[i + k][a]);
            std::sort(win.begin(), win.begin() + w);
            const int m = win[(w - 1) / 2];
            if (m > p[a]) {
                p[a] = m;
                c[a] = i + w / 2;
            }
        }
    if (center) *center = c;
    return p;
}

// Minimum length of a contiguous excursion (same sign, magnitude > threshold)
// that counts as the "first half-movement". Shorter twitches (residual motion
// from the previous step, spike) do not determine the sign.
constexpr int MIN_RUN = 2;

// Sign of the first sustained excursion of 'axis' (>= MIN_RUN consecutive
// samples above 'threshold' with the same sign). If there is none: sign of the
// sum around the peak (peak_center, +-1 sample), 0 without any deflection.
inline int firstExcursionSign(const std::vector<RawAxes> &samples, int axis, int threshold,
                              int peak_center = -1) {
    if (axis < 0 || axis >= AX_COUNT) return 0;
    const int n = int(samples.size());
    int run = 0, run_sign = 0, run_sum = 0;
    for (int i = 0; i < n; ++i) {
        int v = samples[i][axis];
        int sg = std::abs(v) > threshold ? (v > 0 ? 1 : -1) : 0;
        if (sg != 0 && sg == run_sign) {
            ++run;
            run_sum += v;
        } else {
            run = sg != 0 ? 1 : 0;
            run_sign = sg;
            run_sum = sg != 0 ? v : 0;
        }
        if (run >= MIN_RUN) return run_sum > 0 ? 1 : -1;
    }
    long sum = 0;
    if (peak_center < 0) {
        int peak_v = 0;
        for (const auto &s : samples)
            if (std::abs(s[axis]) > std::abs(peak_v)) peak_v = s[axis];
        sum = peak_v;
    } else {
        for (int i = peak_center - 1; i <= peak_center + 1; ++i)
            if (i >= 0 && i < n) sum += samples[i][axis];
    }
    return sum > 0 ? 1 : (sum < 0 ? -1 : 0);
}

inline StepResult analyzeStep(const std::vector<RawAxes> &samples,
                              int sign_threshold = SIGN_THRESHOLD, int min_peak = MIN_PEAK) {
    StepResult r;
    std::array<int, AX_COUNT> center{};
    r.peaks = smoothedPeaks(samples, &center);
    r.axis = strongestAxis(r.peaks);
    if (r.axis < 0) {
        r.too_weak = true;
        // Raw peaks exist but are never sustained -> hint about twitches.
        const auto raw = peakAbs(samples);
        r.message = strongestAxis(raw) >= 0
                        ? "No sustained movement detected (only short deflections) - please "
                          "move steadily and a little longer."
                        : "No movement detected - please move the cap after \"Start\".";
        return r;
    }
    r.peak = r.peaks[r.axis];
    r.runner_up = strongestAxis(r.peaks, r.axis);
    r.ratio = (r.runner_up >= 0 && r.peak > 0) ? double(r.peaks[r.runner_up]) / r.peak : 0.0;
    r.sign = firstExcursionSign(samples, r.axis, sign_threshold, center[r.axis]);
    const std::string an = axisName(Axis(r.axis));
    if (r.peak < min_peak) {
        r.too_weak = true;
        r.message = "Too weak (peak " + std::to_string(r.peak) + " on " + an +
                    ", required " + std::to_string(min_peak) + ") - please move more firmly.";
        return r;
    }
    if (r.ratio >= TIE_RATIO) {
        r.ambiguous = true;
        r.message = "Ambiguous: " + an + " " + std::to_string(r.peak) + " and " +
                    axisName(Axis(r.runner_up)) + " " + std::to_string(r.peaks[r.runner_up]) +
                    " are almost equally strong - please repeat and move more cleanly in only one "
                    "direction.";
        return r;
    }
    r.valid = r.sign != 0;
    r.message = "Detected: " + std::string(r.sign < 0 ? "-" : "+") + an + " (peak " +
                std::to_string(r.peak) + ")";
    if (r.runner_up >= 0)
        r.message += ", second-strongest axis: " + std::string(axisName(Axis(r.runner_up))) + " " +
                     std::to_string(r.peaks[r.runner_up]);
    r.message += ".";
    if (r.ratio > AMBIGUOUS_RATIO)
        r.message += " Close call - repeat if necessary and move more cleanly in only one direction.";
    return r;
}

// Builds an expression valid for spn::parseMix from (axis, sign) terms,
// weight always 1. Duplicate axes are taken only once (the first wins).
// Example: {{AX_Z,+1},{AX_RX,+1}} -> "z + rx"; {{AX_RY,-1}} -> "-ry".
inline std::string buildMix(const std::vector<std::pair<int, int>> &terms) {
    std::string out;
    std::vector<int> used;
    for (const auto &t : terms) {
        if (t.first < 0 || t.first >= AX_COUNT || t.second == 0) continue;
        bool dup = false;
        for (int u : used) dup = dup || u == t.first;
        if (dup) continue;
        used.push_back(t.first);
        const char *n = axisName(Axis(t.first));
        if (out.empty()) out = std::string(t.second < 0 ? "-" : "") + n;
        else out += std::string(t.second < 0 ? " - " : " + ") + n;
    }
    return out;
}

struct WizardResult {
    bool ok = false;                   // all required steps valid -> can be applied
    std::string mix_v, mix_h, mix_zoom;
    std::vector<std::string> errors;   // prevent "Apply"
    std::vector<std::string> warnings; // hints only
};

// Merges the step results. skipped[r] = step deliberately skipped
// (allowed only for optional steps).
inline WizardResult evaluate(const std::array<StepResult, ROLE_COUNT> &res,
                             const std::array<bool, ROLE_COUNT> &skipped) {
    WizardResult w;
    std::array<bool, ROLE_COUNT> used{};
    for (int r = 0; r < ROLE_COUNT; ++r) {
        if (skipped[r] && roleOptional(r)) continue;
        if (skipped[r]) {
            w.errors.push_back(std::string("Step \"") + roleTitle(r) +
                               "\" is required and must not be skipped.");
            continue;
        }
        if (!res[r].valid) {
            w.errors.push_back(std::string("Step \"") + roleTitle(r) +
                               "\" has no valid result" +
                               (res[r].too_weak ? " (too weak)."
                                                 : res[r].ambiguous ? " (ambiguous)." : "."));
            continue;
        }
        used[r] = true;
    }
    // Contradictory roles: the same axis in two steps. Across channels
    // (scroll V/H/zoom) this is an error - otherwise e.g. scroll_v=x and
    // scroll_h=x would be written. Within a channel (push+tilt) the duplicate
    // term is dropped from the mix (buildMix) - this is reported as a warning.
    for (int a = 0; a < ROLE_COUNT; ++a)
        for (int b = a + 1; b < ROLE_COUNT; ++b)
            if (used[a] && used[b] && res[a].axis == res[b].axis) {
                const std::string base = std::string("Axis ") + axisName(Axis(res[a].axis)) +
                                         " was detected for \"" + roleTitle(a) + "\" and for \"" +
                                         roleTitle(b) + "\" - contradictory";
                if (roleChannel(a) != roleChannel(b))
                    w.errors.push_back(base + " (two different channels would use the same "
                                              "axis), please repeat the steps.");
                else
                    w.warnings.push_back(base + "; it is used only once in the mix, please "
                                                "repeat the steps.");
            }
    for (int r = 0; r < ROLE_COUNT; ++r)
        if (used[r] && res[r].axis == AX_Y)
            w.warnings.push_back(std::string("For \"") + roleTitle(r) +
                                 "\" lift/press (y) was strongest - probably "
                                 "pressed down too hard.");

    auto term = [&](int r) {
        return used[r] ? std::make_pair(res[r].axis, res[r].sign) : std::make_pair(-1, 0);
    };
    w.mix_v = buildMix({term(ROLE_PUSH_V), term(ROLE_TILT_V)});
    w.mix_h = buildMix({term(ROLE_PUSH_H), term(ROLE_TILT_H)});
    w.mix_zoom = buildMix({term(ROLE_TWIST)});
    w.ok = w.errors.empty() && !w.mix_v.empty() && !w.mix_h.empty() && !w.mix_zoom.empty();
    return w;
}

}  // namespace wiz
}  // namespace spn
