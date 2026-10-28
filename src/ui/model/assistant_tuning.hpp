// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Thresholds and times of the axis assistant (the flow of src/ui/model/axes_model.hpp,
// WizardController). They are plain keys in ui.conf (axis_assistant_*), documented in docs/ui.md,
// and have no control in the interface. No toolkit.
#pragma once
#include <cstdint>
#include <string>

#include "axis_wizard.hpp"

namespace spn::ui {

struct AxisAssistantTuning {
    int start_threshold = wiz::SIGN_THRESHOLD;  // raw magnitude that starts a step (and counts as moving)
    int rest_ms = 400;                          // rest after the movement that ends a step
    int max_s = 8;                              // longest step (also: no movement -> hint)
    int auto_skip_s = 5;                        // no movement before an optional step is skipped
    // "Don't advance automatically" (check box in the assistant): a detected step stays until Next is pressed and an
    // optional step is not skipped by itself. For slow, keyboard-only or screen reader use. Default off.
    bool manual_advance = false;
    bool operator==(const AxisAssistantTuning &o) const {
        return start_threshold == o.start_threshold && rest_ms == o.rest_ms && max_s == o.max_s &&
               auto_skip_s == o.auto_skip_s && manual_advance == o.manual_advance;
    }
    int64_t restNs() const { return int64_t(rest_ms) * 1000000; }
    int64_t maxNs() const { return int64_t(max_s) * 1000000000; }
    int64_t autoSkipNs() const { return int64_t(auto_skip_s) * 1000000000; }
};

constexpr int kStartThresholdMin = 10, kStartThresholdMax = 200;
constexpr int kRestMsMin = 100, kRestMsMax = 3000;
constexpr int kMaxSMin = 3, kMaxSMax = 60;
constexpr int kAutoSkipSMin = 2, kAutoSkipSMax = 60;

// A decimal integer (optional sign, at most 6 digits) clamped to lo..hi; anything else (empty,
// text, a fraction) gives `def`. A number outside the range is pulled to the nearest limit.
inline int clampedIntOr(const std::string &text, int lo, int hi, int def) {
    size_t i = 0;
    bool neg = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) neg = text[i++] == '-';
    if (i == text.size() || text.size() - i > 6) return def;
    long v = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return def;
        v = v * 10 + (text[i] - '0');
    }
    if (neg) v = -v;
    return int(v < lo ? lo : (v > hi ? hi : v));
}

}  // namespace spn::ui
