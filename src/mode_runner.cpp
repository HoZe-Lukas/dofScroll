// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "mode_runner.hpp"

#include <cmath>

namespace spn {

namespace {

bool buttonHeld(uint32_t mask, int bnum) {
    return bnum >= 0 && bnum < 32 && (mask & (1u << bnum)) != 0;
}

// Below the deadzone (rate 0) drop the remainder, so a started step does not fire
// suddenly on the next tap.
int stepOrReset(StepAccumulator &acc, double rate, double dt) {
    if (rate == 0.0) {
        acc.reset();
        return 0;
    }
    return acc.add(rate, dt);
}

// step: channel action. One tap per push with hysteresis: fires when the rate reaches the trigger on
// a side while armed (+1: positive, -1: negative), re-arms when it has fallen to half the trigger.
// The state is the side that fired last. Returns the signed number of taps (-1, 0 or 1).
int stepFire(int8_t &state, double rate, double trigger) {
    if (state > 0 && rate <= trigger / 2.0) state = 0;
    if (state < 0 && rate >= -trigger / 2.0) state = 0;
    if (state == 0) {
        if (rate >= trigger) { state = 1; return 1; }
        if (rate <= -trigger) { state = -1; return -1; }
    }
    return 0;
}

constexpr size_t idx(Key k) { return static_cast<size_t>(k); }

}  // namespace

ModeRunner::ModeRunner(Output *out) : out_(out) {
    mode_.id = "normal";
    mode_.label = "normal";
}

void ModeRunner::resetState() {
    ch_v_.reset();
    ch_h_.reset();
    ch_zoom_.reset();
    acc_v_.reset();
    acc_h_.reset();
    acc_zoom_.reset();
    step_v_ = step_h_ = step_zoom_ = 0;
    for (int i = 0; i < PART_COUNT; ++i) {
        ch_part_[i].reset();
        acc_part_[i].reset();
        step_part_[i] = 0;
    }
    zoom_.reset();
}

void ModeRunner::setMode(const ModeDef &mode) {
    releaseAll();
    nav_latched_ = false;
    mode_ = mode;
    legacy_ = mode_.allDefaultChannels();
}

void ModeRunner::releaseAll() {
    syncKeys(std::bitset<kKeyCount>());
    syncMouse(0);
    resetState();
}

void ModeRunner::resetDsp() {
    resetState();
    if (keys_held_[idx(Key::Ctrl)]) {
        out_->setKey(Key::Ctrl, false);
        keys_held_[idx(Key::Ctrl)] = false;
    }
}

void ModeRunner::syncMouse(uint8_t want) {
    for (int b = 1; b <= 3; ++b) {
        const bool w = (want >> b) & 1, h = (mouse_held_ >> b) & 1;
        if (w != h) out_->mouseButton(b, w);
    }
    mouse_held_ = want;
}

void ModeRunner::syncKeys(const std::bitset<kKeyCount> &want) {
    for (size_t i = 0; i < kKeyCount; ++i) {
        if (want[i] != keys_held_[i]) out_->setKey(static_cast<Key>(i), want[i]);
    }
    keys_held_ = want;
}

ModeRunner::Wanted ModeRunner::bindings(const Settings &s, uint32_t buttons) const {
    Wanted w;
    auto apply = [&w](const ButtonBinding &b) {
        switch (b.type) {
        case ButtonActionType::Unbound:
        case ButtonActionType::ModeSwitch:
        case ButtonActionType::ToggleNav:
        case ButtonActionType::ToggleMode:
        case ButtonActionType::HoldMode:
        case ButtonActionType::ButtonMode: break;  // act on the press edge, see tick(); ButtonMode is expanded by the daemon core
        case ButtonActionType::Click: w.mouse |= 1u << 1; break;
        case ButtonActionType::MiddleClick: w.mouse |= 1u << 2; break;
        case ButtonActionType::RightClick: w.mouse |= 1u << 3; break;
        case ButtonActionType::Nav: w.nav = true; break;
        case ButtonActionType::Key: w.keys[idx(b.key)] = true; break;
        }
    };
    for (const ButtonBinding &b : mode_.buttons) {
        if (!buttonHeld(buttons, b.button - 1)) continue;  // bindings are 1-based
        apply(b);
    }
    // BUTTON_<n> from the resolved profile; the mode's own binding for n wins.
    for (const auto &kv : s.buttons) {
        if (!buttonHeld(buttons, kv.first - 1)) continue;
        bool in_mode = false;
        for (const ButtonBinding &b : mode_.buttons)
            if (b.button == kv.first) in_mode = true;
        if (in_mode) continue;
        ButtonBinding b;
        b.button = kv.first;
        if (!parseButtonAction(kv.second, &b.type, &b.key, nullptr)) continue;
        apply(b);
    }
    if (nav_latched_) w.nav = true;
    return w;
}

std::vector<ButtonBinding> ModeRunner::edgeBindings(const Settings &s) const {
    auto isEdge = [](ButtonActionType t) {
        return t == ButtonActionType::ModeSwitch || t == ButtonActionType::ToggleNav || t == ButtonActionType::ToggleMode ||
               t == ButtonActionType::HoldMode;
    };
    std::vector<ButtonBinding> out;
    for (const ButtonBinding &b : mode_.buttons)
        if (isEdge(b.type)) out.push_back(b);
    for (const auto &kv : s.buttons) {
        bool in_mode = false;
        for (const ButtonBinding &b : mode_.buttons)
            if (b.button == kv.first) in_mode = true;
        if (in_mode) continue;
        ButtonBinding b;
        b.button = kv.first;
        if (!parseButtonAction(kv.second, &b.type, &b.key, nullptr, &b.mode)) continue;
        if (isEdge(b.type)) out.push_back(b);
    }
    return out;
}

void ModeRunner::tapKeys(Key neg, Key pos, int n) {
    for (; n > 0; --n) out_->tapKey(pos);
    for (; n < 0; ++n) out_->tapKey(neg);
}

// Presses the modifiers that are not held yet (in the order written), taps the key and releases
// only the modifiers pressed here (reverse order). A modifier held by a button or by zoom stays.
void ModeRunner::tapChord(Key key, const ChordMods &mods) {
    Key pressed[kMaxChordMods];
    uint8_t np = 0;
    for (uint8_t i = 0; i < mods.n; ++i)
        if (!keys_held_[idx(mods.k[i])]) {
            out_->setKey(mods.k[i], true);
            pressed[np++] = mods.k[i];
        }
    out_->tapKey(key);
    while (np > 0) out_->setKey(pressed[--np], false);
}

void ModeRunner::tick(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh,
                      double dt) {
    // Release of the button that started a hold:mode: (remembered here, the mode that is active
    // while holding need not bind it). Reported like a switch; the daemon ignores it when the hold
    // has been ended by another switch.
    const uint32_t released = prev_buttons_ & ~buttons;
    if (hold_release_pending_ && switch_request_.empty()) {
        switch_request_ = kHoldReleaseRequest;
        hold_release_pending_ = false;
    }
    if (hold_button_ > 0 && buttonHeld(released, hold_button_ - 1)) {
        hold_button_ = 0;
        if (switch_request_.empty()) switch_request_ = kHoldReleaseRequest;
        else hold_release_pending_ = true;
    }
    // Press edges only; nothing to look at (or to parse) while no button went down.
    const uint32_t pressed = buttons & ~prev_buttons_;
    for (const ButtonBinding &b : pressed ? edgeBindings(s) : std::vector<ButtonBinding>()) {
        if (!buttonHeld(pressed, b.button - 1)) continue;
        if (b.type == ButtonActionType::ToggleNav) {
            nav_latched_ = !nav_latched_;
        } else if (switch_request_.empty()) {
            if (b.type == ButtonActionType::HoldMode) {
                switch_request_ = "hold:" + b.mode;
                hold_button_ = b.button;
            } else {
                switch_request_ = b.type == ButtonActionType::ToggleMode ? "toggle:" + b.mode : b.mode;
            }
        }
    }
    prev_buttons_ = buttons;
    if (legacy_) tickLegacy(s, raw, buttons, fresh, dt);
    else tickCustom(s, raw, buttons, fresh, dt);
}

// Legacy path: same logic as before modes existed, plus the mode's button bindings.
void ModeRunner::tickLegacy(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh,
                            double dt) {
    Wanted w = bindings(s, buttons);
    const bool nav_active =
        fresh && ((s.nav_button_enabled && buttonHeld(buttons, s.nav_button)) || w.nav);

    // Click button 1:1 as left mouse button (press + release separate), so click-and-hold
    // drags work.
    if (s.click_button_enabled && buttonHeld(buttons, s.click_button)) w.mouse |= 1u << 1;
    syncMouse(w.mouse);

    // Scroll channels run every tick (smoothing state stays continuous); what is sent
    // depends on the mode.
    double rate_v = ch_v_.process(s.scroll_v, raw, dt);
    double rate_h = ch_h_.process(s.scroll_h, raw, dt);

    // Zoom = twist; entry only if it dominates the other axes (hysteresis/delay in
    // ZoomDetector). Off in nav mode.
    double zoom_mixed = evalMix(s.zoom.mix, raw);
    double twist = nav_active ? 0.0 : std::fabs(zoom_mixed * s.zoom.sign);
    double lateral = lateralMax(s.zoom_lateral, raw);
    if (s.zoom.sensitivity <= 0) twist = 0.0;
    bool zooming = zoom_.update(twist, lateral, s, dt);
    double rate_zoom = ch_zoom_.processValue(s.zoom, zoom_mixed, dt);

    std::bitset<kKeyCount> keys = w.keys;
    if (zooming) keys[idx(Key::Ctrl)] = true;
    syncKeys(keys);
    if (zooming) {
        acc_v_.reset();
        acc_h_.reset();
        // Positive rate = scroll up = zoom in; wheelV(n > 0) = down.
        int n = acc_zoom_.add(rate_zoom, dt);
        if (n != 0) out_->wheelV(-n);
    } else {
        acc_zoom_.reset();
        // Positive rate = down or right.
        int nv = stepOrReset(acc_v_, rate_v, dt);
        int nh = stepOrReset(acc_h_, rate_h, dt);
        if (nav_active) {
            tapKeys(Key::Up, Key::Down, nv);
            tapKeys(Key::Left, Key::Right, nh);
        } else {
            if (nv != 0) out_->wheelV(nv);
            if (nh != 0) out_->wheelH(nh);
        }
    }
    info_ = Info{rate_h, rate_v, twist, lateral, zooming, nav_active, rate_zoom};
}

// Custom path: the channels follow the mode's actions. Ctrl is global to the output, so a
// zoom action on one channel also affects wheel events of another channel in that tick.
// Sign convention: scroll channels (V, H): positive = down/right. Twist: positive = zoom
// in = wheel up, so wheel actions on the twist are inverted on the vertical wheel.
void ModeRunner::tickCustom(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh,
                            double dt) {
    Wanted w = bindings(s, buttons);
    const bool nav_active = fresh && w.nav;
    syncMouse(w.mouse);

    const ChannelAction &av = mode_.v, &ah = mode_.h, &at = mode_.twist;
    auto eff = [](const ChannelAction &a, ChannelActionType dflt) {
        return a.type == ChannelActionType::Default ? dflt : a.type;
    };
    const ChannelActionType tv = eff(av, ChannelActionType::WheelV);
    const ChannelActionType th = eff(ah, ChannelActionType::WheelH);
    const ChannelActionType tt = eff(at, ChannelActionType::Zoom);

    // A split channel runs its push and tilt parts as sub-channels (parent's settings + per-part
    // tuning, the part's share of the mix); the mixed channel then stays idle.
    const bool split[2] = {mode_.vSplit.active(), mode_.hSplit.active()};
    ChannelActionType part_t[PART_COUNT] = {tv, tv, th, th};
    const ChannelAction *part_a[PART_COUNT] = {&av, &av, &ah, &ah};
    double part_rate[PART_COUNT] = {0, 0, 0, 0};
    double rate_v = 0.0, rate_h = 0.0;
    if (split[0]) {
        part_a[PART_V_PUSH] = &mode_.vSplit.pushAction(av);
        part_a[PART_V_TILT] = &mode_.vSplit.tiltAction(av);
    }
    if (split[1]) {
        part_a[PART_H_PUSH] = &mode_.hSplit.pushAction(ah);
        part_a[PART_H_TILT] = &mode_.hSplit.tiltAction(ah);
    }
    for (int c = 0; c < 2; ++c) {
        const ChannelCfg &parent = c == 0 ? s.scroll_v : s.scroll_h;
        double &rate = c == 0 ? rate_v : rate_h;
        if (!split[c]) {
            rate = (c == 0 ? ch_v_ : ch_h_).process(parent, raw, dt);
            continue;
        }
        Mix push, tilt;
        splitMix(parent.mix, &push, &tilt);
        for (int k = 0; k < 2; ++k) {
            const int i = c * 2 + k;
            part_t[i] = eff(*part_a[i], c == 0 ? ChannelActionType::WheelV : ChannelActionType::WheelH);
            part_rate[i] = ch_part_[i].process(partConfig(parent, s.parts[i], k == 0 ? push : tilt), raw, dt);
        }
        const double a = part_rate[c * 2], b = part_rate[c * 2 + 1];
        rate = std::fabs(a) >= std::fabs(b) ? a : b;
    }

    // Twist with zoom keeps the dominance detection; all other actions are plain.
    double zoom_mixed = evalMix(s.zoom.mix, raw);
    double twist = std::fabs(zoom_mixed * s.zoom.sign);
    double lateral = lateralMax(s.zoom_lateral, raw);
    if (nav_active || s.zoom.sensitivity <= 0) twist = 0.0;
    bool zooming = false;
    if (tt == ChannelActionType::Zoom) zooming = zoom_.update(twist, lateral, s, dt);
    else zoom_.reset();
    double rate_zoom = ch_zoom_.processValue(s.zoom, zoom_mixed, dt);

    int nv = 0, nh = 0, nt = 0;
    int np[PART_COUNT] = {0, 0, 0, 0};
    if (zooming) {
        acc_v_.reset();
        acc_h_.reset();
        step_v_ = step_h_ = 0;
        for (int i = 0; i < PART_COUNT; ++i) {
            acc_part_[i].reset();
            step_part_[i] = 0;
        }
        nt = acc_zoom_.add(rate_zoom, dt);
    } else {
        // Channels with a step: action use the armed/fired state instead of the accumulator.
        auto channel = [&](ChannelActionType t, StepAccumulator &acc, int8_t &step, double rate) {
            if (t == ChannelActionType::Step) {
                acc.reset();
                return stepFire(step, rate, s.step_trigger);
            }
            step = 0;
            if (t == ChannelActionType::Off) {
                acc.reset();
                return 0;
            }
            return stepOrReset(acc, rate, dt);
        };
        // The twist channel does not accumulate for zoom itself (the zoom detection does).
        nt = channel(tt == ChannelActionType::Zoom ? ChannelActionType::Off : tt, acc_zoom_, step_zoom_, rate_zoom);
        if (split[0]) {
            for (int i = PART_V_PUSH; i <= PART_V_TILT; ++i) np[i] = channel(part_t[i], acc_part_[i], step_part_[i], part_rate[i]);
        } else {
            nv = channel(tv, acc_v_, step_v_, rate_v);
        }
        if (split[1]) {
            for (int i = PART_H_PUSH; i <= PART_H_TILT; ++i) np[i] = channel(part_t[i], acc_part_[i], step_part_[i], part_rate[i]);
        } else {
            nh = channel(th, acc_h_, step_h_, rate_h);
        }
    }

    std::bitset<kKeyCount> keys = w.keys;
    bool part_zoom = false;
    for (int i = 0; i < PART_COUNT; ++i)
        if (split[i / 2] && part_t[i] == ChannelActionType::Zoom && part_rate[i] != 0.0) part_zoom = true;
    if (zooming || (!split[0] && tv == ChannelActionType::Zoom && rate_v != 0.0) ||
        (!split[1] && th == ChannelActionType::Zoom && rate_h != 0.0) || part_zoom)
        keys[idx(Key::Ctrl)] = true;
    syncKeys(keys);

    auto emit = [&](const ChannelAction &a, ChannelActionType t, int n, int sgn) {
        if (n == 0) return;
        switch (t) {
        case ChannelActionType::Off:
        case ChannelActionType::Default: break;
        case ChannelActionType::Zoom: out_->wheelV(sgn * n); break;
        case ChannelActionType::WheelV:
            if (nav_active) tapKeys(Key::Up, Key::Down, sgn * n);
            else out_->wheelV(sgn * n);
            break;
        case ChannelActionType::WheelH:
            if (nav_active) tapKeys(Key::Left, Key::Right, n);
            else out_->wheelH(n);
            break;
        case ChannelActionType::Step:
        case ChannelActionType::Keys:
            for (int i = n; i > 0; --i) tapChord(a.pos, a.posMods);
            for (int i = n; i < 0; ++i) tapChord(a.neg, a.negMods);
            break;
        }
    };
    emit(av, tv, nv, 1);
    for (int i = PART_V_PUSH; i <= PART_V_TILT; ++i) emit(*part_a[i], part_t[i], np[i], 1);
    emit(ah, th, nh, 1);
    for (int i = PART_H_PUSH; i <= PART_H_TILT; ++i) emit(*part_a[i], part_t[i], np[i], 1);
    emit(at, tt, nt, -1);
    info_ = Info{rate_h, rate_v, twist, lateral, zooming, nav_active, rate_zoom};
    for (int i = 0; i < PART_COUNT; ++i) info_.rate_part[i] = split[i / 2] ? part_rate[i] : 0.0;
}

}  // namespace spn
