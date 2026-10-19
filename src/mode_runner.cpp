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
    zoom_.reset();
}

void ModeRunner::setMode(const ModeDef &mode) {
    releaseAll();
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
        case ButtonActionType::ModeSwitch: break;  // ModeSwitch acts on the press edge, see tick()
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
    return w;
}

void ModeRunner::tapKeys(Key neg, Key pos, int n) {
    for (; n > 0; --n) out_->tapKey(pos);
    for (; n < 0; ++n) out_->tapKey(neg);
}

void ModeRunner::tick(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh,
                      double dt) {
    for (const ButtonBinding &b : mode_.buttons) {
        if (b.type != ButtonActionType::ModeSwitch || switch_request_.size()) continue;
        if (buttonHeld(buttons, b.button - 1) && !buttonHeld(prev_buttons_, b.button - 1)) switch_request_ = b.mode;
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

    double rate_v = ch_v_.process(s.scroll_v, raw, dt);
    double rate_h = ch_h_.process(s.scroll_h, raw, dt);

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
    if (zooming) {
        acc_v_.reset();
        acc_h_.reset();
        nt = acc_zoom_.add(rate_zoom, dt);
    } else {
        if (tt == ChannelActionType::Zoom || tt == ChannelActionType::Off) acc_zoom_.reset();
        else nt = stepOrReset(acc_zoom_, rate_zoom, dt);
        if (tv != ChannelActionType::Off) nv = stepOrReset(acc_v_, rate_v, dt);
        else acc_v_.reset();
        if (th != ChannelActionType::Off) nh = stepOrReset(acc_h_, rate_h, dt);
        else acc_h_.reset();
    }

    std::bitset<kKeyCount> keys = w.keys;
    if (zooming || (tv == ChannelActionType::Zoom && rate_v != 0.0) ||
        (th == ChannelActionType::Zoom && rate_h != 0.0))
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
        case ChannelActionType::Keys: tapKeys(a.neg, a.pos, n); break;
        }
    };
    emit(av, tv, nv, 1);
    emit(ah, th, nh, 1);
    emit(at, tt, nt, -1);
    info_ = Info{rate_h, rate_v, twist, lateral, zooming, nav_active, rate_zoom};
}

}  // namespace spn
