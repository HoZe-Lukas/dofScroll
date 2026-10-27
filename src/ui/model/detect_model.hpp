// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Detect by moving" of the visual joystick mode editor (issue #143, toolkit-free): turns the samples of the
// live feed into "this motion was selected" or "this button was pressed". It reuses the rules of the axis
// wizard (src/axis_wizard.hpp): the strongest axis wins after smoothing (a single spike does not count),
// a movement below MIN_PEAK is too weak, and two nearly equally strong axes (TIE_RATIO) are ambiguous, so
// crosstalk of a push on the other axes does not select the wrong motion.
//
// One selection per movement: after a motion was reported nothing more is reported until the cap came back
// to rest (every axis below REST_LEVEL) for a few samples, so holding the cap does not flicker between
// motions. A button press is reported on its rising edge, the lowest new button first.
//
//   DetectModel d;
//   DetectEvent e = d.feed(sample.axes, sample.buttons);   // per sample
//   if (e.kind == DetectEvent::Motion) select(roles.motionOf(e.axis), e.sign);
//   if (e.kind == DetectEvent::Button) selectButton(e.button);
//   d.reset();                                              // when Detect is switched on or off
#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <vector>

#include "axis_wizard.hpp"
#include "common.hpp"
#include "ui/model/motion_map_model.hpp"

namespace spn::ui {

struct DetectEvent {
    enum Kind { None, Motion, Button } kind = None;
    Axis axis = AX_X;  // the raw axis that moved; the roles of the configuration (axis_roles_model.hpp) say which motion it is
    int sign = 0;     // +1 / -1: the direction of the first sustained excursion
    int button = 0;   // 1-based
};

class DetectModel {
public:
    static constexpr int kWindow = 8;     // samples looked at (about a quarter second at the 30 Hz feed)
    static constexpr int kRestLevel = 25;  // below this on every axis the cap counts as at rest
    static constexpr int kRestSamples = 4;

    void reset() {
        window_.clear();
        armed_ = true;
        rest_ = 0;
        buttons_ = 0;
        seeded_ = false;
    }

    DetectEvent feed(const RawAxes &axes, uint32_t buttons) {
        DetectEvent e;
        if (!seeded_) {  // buttons already down when Detect starts are not presses
            buttons_ = buttons;
            seeded_ = true;
        }
        const uint32_t fresh = buttons & ~buttons_;
        buttons_ = buttons;
        if (fresh) {
            for (int b = 0; b < 32; ++b)
                if (fresh & (1u << b)) {
                    e.kind = DetectEvent::Button;
                    e.button = b + 1;
                    return e;
                }
        }
        window_.push_back(axes);
        if (int(window_.size()) > kWindow) window_.pop_front();
        int strongest = 0;
        for (int a = 0; a < AX_COUNT; ++a) strongest = std::max(strongest, std::abs(axes[a]));
        if (strongest < kRestLevel) {
            if (++rest_ >= kRestSamples) {
                armed_ = true;
                window_.clear();
            }
            return e;
        }
        rest_ = 0;
        if (!armed_ || int(window_.size()) < wiz::SMOOTH_WIN) return e;  // a lone sample is a spike, not a movement
        const std::vector<RawAxes> samples(window_.begin(), window_.end());
        std::array<int, AX_COUNT> center{};
        const std::array<int, AX_COUNT> peaks = wiz::smoothedPeaks(samples, &center);
        const int axis = wiz::strongestAxis(peaks);
        if (axis < 0 || peaks[axis] < wiz::MIN_PEAK) return e;
        const int second = wiz::strongestAxis(peaks, axis);
        if (second >= 0 && double(peaks[second]) / peaks[axis] >= wiz::TIE_RATIO) return e;  // ambiguous: wait for a cleaner move
        const int sign = wiz::firstExcursionSign(samples, axis, wiz::SIGN_THRESHOLD, center[axis]);
        if (sign == 0) return e;
        armed_ = false;
        e.kind = DetectEvent::Motion;
        e.axis = Axis(axis);
        e.sign = sign;
        return e;
    }

private:
    std::deque<RawAxes> window_;
    bool armed_ = true;
    int rest_ = 0;
    uint32_t buttons_ = 0;
    bool seeded_ = false;
};

}  // namespace spn::ui
