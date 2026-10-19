// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ModeRunner: the per-tick evaluation of the active mode (see modes.hpp). It owns the DSP
// state (channels, step accumulators, zoom detector) and everything that is held down on
// the output (keys, mouse buttons), and talks to the output only through the Output
// interface, so it can be unit-tested with a fake output.
//
// Two paths:
//  - legacy: all three channel actions are "default". Identical to the behavior before
//    modes existed (dominance-based zoom, NAV_BUTTON / CLICK_BUTTON from the settings).
//  - custom: at least one channel action differs from "default". The channels follow the
//    mode's actions; NAV_BUTTON / CLICK_BUTTON are ignored (the mode's button bindings
//    replace them).
// Button bindings of the mode apply in both paths.
//
// Everything held (Ctrl for zoom, key/mouse bindings) is released on mode change and on
// releaseAll() (suspend, ENABLED=0, exit).
#pragma once
#include <bitset>
#include <cmath>
#include <cstdint>

#include "common.hpp"
#include "config.hpp"
#include "dsp.hpp"
#include "keys.hpp"
#include "modes.hpp"
#include "output.hpp"

namespace spn {

class ModeRunner {
public:
    // Rates (steps/s) below this count as "no deflection" (smoothing tails without dead zone).
    static constexpr double kRateEpsilon = 1e-3;

    // out must outlive the runner. Starts in a legacy-path "normal" mode.
    explicit ModeRunner(Output *out);

    // Activates a mode: releases everything held and resets the DSP state.
    void setMode(const ModeDef &mode);
    const ModeDef &mode() const { return mode_; }
    // Target of a mode:<id> / mode:next binding whose button was pressed in the last tick
    // (press edge only); empty if none. Reading clears it. The caller performs the switch.
    std::string takeSwitchRequest() {
        std::string r;
        r.swap(switch_request_);
        return r;
    }
    bool legacyPath() const { return legacy_; }

    // One tick. raw/buttons are already zeroed by the caller while suspended or disabled;
    // fresh = the device state is recent enough to count for nav (stale watchdog).
    // Does not call Output::flush().
    void tick(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh, double dt);

    // Releases every held key and mouse button and resets the DSP state.
    void releaseAll();
    // Settings changed (profile switch): resets the DSP state and releases the zoom Ctrl,
    // but not mouse buttons (a running drag survives).
    void resetDsp();

    struct Info {  // for --debug
        double rate_h = 0, rate_v = 0, twist = 0, lateral = 0;
        bool zooming = false, nav = false;
        double rate_zoom = 0;
    };
    const Info &info() const { return info_; }

    // Something is held on the output (a key or a mouse button).
    bool holdsAnything() const { return keys_held_.any() || mouse_held_ != 0; }
    // An axis channel is outside its dead zone (its rate after smoothing, dead zone and
    // curve is not zero), or the zoom detector is latched. Used by the daemon's safety net.
    bool axesActive() const {
        return std::fabs(info_.rate_h) > kRateEpsilon || std::fabs(info_.rate_v) > kRateEpsilon ||
               std::fabs(info_.rate_zoom) > kRateEpsilon || info_.zooming;
    }

private:
    Output *out_;
    ModeDef mode_;
    bool legacy_ = true;
    uint32_t prev_buttons_ = 0;
    std::string switch_request_;

    Channel ch_v_, ch_h_, ch_zoom_;
    StepAccumulator acc_v_, acc_h_, acc_zoom_;
    ZoomDetector zoom_;
    std::bitset<kKeyCount> keys_held_;
    uint8_t mouse_held_ = 0;  // bit n = mouse button n (1 left, 2 middle, 3 right)
    Info info_;

    // Desired state derived from the bindings.
    struct Wanted {
        std::bitset<kKeyCount> keys;
        uint8_t mouse = 0;
        bool nav = false;
    };
    Wanted bindings(const Settings &s, uint32_t buttons) const;
    void syncMouse(uint8_t want);
    void syncKeys(const std::bitset<kKeyCount> &want);
    void resetState();
    void tapKeys(Key neg, Key pos, int n);
    void tickLegacy(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh, double dt);
    void tickCustom(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh, double dt);
};

}  // namespace spn
