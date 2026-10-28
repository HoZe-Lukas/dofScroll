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
//  - custom: at least one channel action differs from "default" (the lift counts only when it does something). The channels follow the
//    mode's actions; NAV_BUTTON / CLICK_BUTTON are ignored (the mode's button bindings
//    replace them).
//    A mode that splits the V and/or H channel into push and tilt (modes.hpp, SplitChannel) runs the
//    parts as independent sub-channels with their own filter state, step accumulator and step: state;
//    everything they hold is released like the rest on mode change and releaseAll().
// Button bindings of the mode apply in both paths.
//
// Everything held (Ctrl for zoom, key/mouse bindings) is released on mode change and on
// releaseAll() (suspend, ENABLED=0, exit).
#pragma once
#include <bitset>
#include <cmath>
#include <cstdint>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "dsp.hpp"
#include "key_engine.hpp"
#include "keys.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "pointer_engine.hpp"
#include "profile_actions.hpp"

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
    // A hold:mode:<id> press is reported as "hold:<id>", the release of that button as
    // kHoldReleaseRequest (contains a colon, so it can never be a mode id: a mode file "release.conf"
    // is switched to by mode:release like any other)
    // (also when the mode that is active while holding has no binding for the button).
    // A toggle:mode:<id> press is reported as "toggle:<id>": the caller switches to <id>, or, if
    // <id> is already the effective mode because of such a switch, drops the switch.
    static constexpr const char *kHoldReleaseRequest = "release:hold";
    std::string takeSwitchRequest() {
        std::string r;
        r.swap(switch_request_);
        return r;
    }
    bool legacyPath() const { return legacy_; }
    // toggle:nav state: the navigation mode is latched on (a press of the button flips it).
    bool navLatched() const { return nav_latched_; }
    // Clears the toggle:nav latch. Called on reload, "Release all keys", suspend/disabled and an
    // unplugged device; releaseAll() (also used by the safety net) leaves it alone, and setMode()
    // clears it.
    void clearToggles() {
        nav_latched_ = false;
        hold_button_ = 0;
        hold_release_pending_ = false;
    }

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
        double rate_lift = 0;  // the lift / press motion (0 while its action is none)
        // Rates of the four sub-channels (SplitPart order), 0 for a part that does not run. For a split
        // channel rate_v / rate_h hold the part rate with the larger magnitude (signed), so that the live
        // feed and the debug output still show the channel's output.
        double rate_part[PART_COUNT] = {0, 0, 0, 0};
    };
    const Info &info() const { return info_; }

    // Something is held on the output (a key or a mouse button).
    bool holdsAnything() const { return keys_held_.any() || mouse_held_ != 0; }
    // An axis channel is outside its dead zone (its rate after smoothing, dead zone and
    // curve is not zero), or the zoom detector is latched. Used by the daemon's safety net.
    bool axesActive() const {
        for (double r : info_.rate_part)
            if (std::fabs(r) > kRateEpsilon) return true;
        return std::fabs(info_.rate_h) > kRateEpsilon || std::fabs(info_.rate_v) > kRateEpsilon ||
               std::fabs(info_.rate_zoom) > kRateEpsilon || std::fabs(info_.rate_lift) > kRateEpsilon || info_.zooming;
    }

private:
    Output *out_;
    ModeDef mode_;
    bool legacy_ = true;
    uint32_t prev_buttons_ = 0;
    std::string switch_request_;
    bool nav_latched_ = false;
    int hold_button_ = 0;  // button (1-based) that started a hold:mode:, 0 = none
    bool hold_release_pending_ = false;

    Channel ch_v_, ch_h_, ch_zoom_, ch_lift_;
    StepAccumulator acc_v_, acc_h_, acc_zoom_, acc_lift_;
    // Key engine slots, one per motion (KeyMotion order; a channel that is not split uses the push slot): keys: is a
    // KeyRepeater, step: a DigitalTrigger (key_engine.hpp).
    KeyRepeater rep_[KM_COUNT];
    DigitalTrigger trig_[KM_COUNT];
    PointerMover ptr_[KM_COUNT];                      // pointer_x / pointer_y: pixel remainder per motion (pointer_engine.hpp)
    Channel ch_part_[PART_COUNT];                     // sub-channels of split V / H channels
    StepAccumulator acc_part_[PART_COUNT];
    ZoomDetector zoom_;
    bool custom_ = false;                  // the last tick ran on the custom path (its slots may hold state)
    double hold_time_[KM_COUNT] = {0, 0, 0, 0, 0, 0};  // seconds the hold: of a motion has been held
    bool hold_logged_[KM_COUNT] = {false, false, false, false, false, false};
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
    // Bindings that act on the press edge (mode switches and toggles): the mode's own, then
    // BUTTON_<n> of the resolved profile for buttons the mode does not bind.
    std::vector<ButtonBinding> edgeBindings(const Settings &s) const;
    void syncMouse(uint8_t want);
    void syncKeys(const std::bitset<kKeyCount> &want);
    void resetState();
    void resetCustomState();  // the state only the custom path uses (lift, key slots, split parts, holds)
    void tapKeys(Key neg, Key pos, int n);
    void tapChord(Key key, const ChordMods &mods);
    void tickLegacy(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh, double dt);
    void tickCustom(const Settings &s, const ChannelActions &ca, const RawAxes &raw, uint32_t buttons, bool fresh, double dt);
};

}  // namespace spn
