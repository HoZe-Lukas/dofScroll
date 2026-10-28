// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "mode_runner.hpp"
#include "profile_actions.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

const char *const LOG = "[dofscroll] ";
constexpr double kHoldLogSeconds = 60.0;  // a hold longer than this is logged once (never released by the runner)

constexpr size_t idx(Key k) { return static_cast<size_t>(k); }

}  // namespace

ModeRunner::ModeRunner(Output *out) : out_(out) {
    mode_.id = "normal";
    mode_.label = "normal";
}

// What only the custom path uses: the lift's filter and accumulator, the key slots (repeater, digital trigger and with it a
// hold:), the split parts. Called whenever the runner leaves the custom path, so a stale filter or an armed / fired step
// flag cannot act when the custom path comes back.
void ModeRunner::resetCustomState() {
    ch_lift_.reset();
    acc_lift_.reset();
    for (int i = 0; i < KM_COUNT; ++i) {
        rep_[i].reset();
        trig_[i].reset();
        hold_time_[i] = 0.0;
        hold_logged_[i] = false;
        ptr_[i].reset();
    }
    for (int i = 0; i < PART_COUNT; ++i) {
        ch_part_[i].reset();
        acc_part_[i].reset();
    }
}

void ModeRunner::resetState() {
    ch_v_.reset();
    ch_h_.reset();
    ch_zoom_.reset();
    acc_v_.reset();
    acc_h_.reset();
    acc_zoom_.reset();
    resetCustomState();
    zoom_.reset();
}

void ModeRunner::setMode(const ModeDef &mode) {
    releaseAll();
    nav_latched_ = false;
    mode_ = mode;
    legacy_ = mode_.allDefaultChannels();  // until the first tick; tick() then follows the effective actions (profile layers)
}

void ModeRunner::releaseAll() {
    syncKeys(std::bitset<kKeyCount>());
    syncMouse(0);
    resetState();
}

void ModeRunner::resetDsp() {
    resetState();
    // The zoom keys may differ in the new settings (ZOOM_MODIFIER): release every modifier; a button binding
    // that holds one is pressed again on the next tick.
    for (Key k : {Key::Ctrl, Key::Shift, Key::Alt, Key::Super}) {
        if (!keys_held_[idx(k)]) continue;
        out_->setKey(k, false);
        keys_held_[idx(k)] = false;
    }
}

// The keys held while a zoom action sends the mouse wheel (ZOOM_MODIFIER).
static void addZoomKeys(std::bitset<kKeyCount> &keys, const Settings &s) {
    if (s.zoom_modifier & kZoomModCtrl) keys[idx(Key::Ctrl)] = true;
    if (s.zoom_modifier & kZoomModShift) keys[idx(Key::Shift)] = true;
    if (s.zoom_modifier & kZoomModAlt) keys[idx(Key::Alt)] = true;
    if (s.zoom_modifier & kZoomModSuper) keys[idx(Key::Super)] = true;
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
        case ButtonActionType::Key:
            if (b.mods.n == 0) w.keys[idx(b.key)] = true;  // a chord is tapped on the press edge, see tick()
            break;
        }
    };
    for (const ButtonBinding &b : mode_.buttons) {
        if (!buttonHeld(buttons, b.button - 1)) continue;  // bindings are 1-based
        if (s.app_buttons.count(b.button)) continue;       // a program profile binds this button itself: it wins
        apply(b);
    }
    // BUTTON_<n> from the resolved profile: the mode's own binding for n wins over [global] and [device:*], a
    // BUTTON_<n> of an [app:*] profile wins over the mode (the program has the last word; "none" unbinds).
    for (const auto &kv : s.buttons) {
        if (!buttonHeld(buttons, kv.first - 1)) continue;
        bool in_mode = false;
        for (const ButtonBinding &b : mode_.buttons)
            if (b.button == kv.first) in_mode = true;
        if (in_mode && !s.app_buttons.count(kv.first)) continue;
        ButtonBinding b;
        b.button = kv.first;
        if (!parseButtonAction(kv.second, &b.type, &b.key, nullptr, nullptr, &b.mods)) continue;
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
    // a key chord (key:ctrl+0) acts once on the press edge too
    auto isChord = [](const ButtonBinding &b) { return b.type == ButtonActionType::Key && b.mods.n > 0; };
    std::vector<ButtonBinding> out;
    for (const ButtonBinding &b : mode_.buttons)
        if ((isEdge(b.type) || isChord(b)) && !s.app_buttons.count(b.button)) out.push_back(b);
    for (const auto &kv : s.buttons) {
        bool in_mode = false;
        for (const ButtonBinding &b : mode_.buttons)
            if (b.button == kv.first) in_mode = true;
        if (in_mode && !s.app_buttons.count(kv.first)) continue;
        ButtonBinding b;
        b.button = kv.first;
        if (!parseButtonAction(kv.second, &b.type, &b.key, nullptr, &b.mode, &b.mods)) continue;
        if (isEdge(b.type) || isChord(b)) out.push_back(b);
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
        if (b.type == ButtonActionType::Key) {
            tapChord(b.key, b.mods);  // one tap per press, nothing on release: the modifiers pressed here are released at once
        } else if (b.type == ButtonActionType::ToggleNav) {
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
    RawAxes axes = raw;
    // INVERT_AXES: flip the chosen axes before any mix, split part or zoom detection sees them
    for (int a = 0; a < AX_COUNT; ++a)
        if (s.invert_axes & (1u << a)) axes[size_t(a)] = -axes[size_t(a)];
    const ChannelActions ca = effectiveChannelActions(mode_, s);
    legacy_ = ca.allDefault();
    if (legacy_) tickLegacy(s, axes, buttons, fresh, dt);
    else tickCustom(s, ca, axes, buttons, fresh, dt);
}

// Legacy path: same logic as before modes existed, plus the mode's button bindings.
void ModeRunner::tickLegacy(const Settings &s, const RawAxes &raw, uint32_t buttons, bool fresh,
                            double dt) {
    if (custom_) {  // coming back from the custom path: nothing of its state may linger
        resetCustomState();
        custom_ = false;
    }
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
    double lateral = lateralMax(s.zoom_lateral, raw, s.zoom.mix);
    if (s.zoom.sensitivity <= 0) twist = 0.0;
    bool zooming = zoom_.update(twist, lateral, s, dt);
    double rate_zoom = ch_zoom_.processValue(s.zoom, zoom_mixed, dt);

    std::bitset<kKeyCount> keys = w.keys;
    if (zooming) addZoomKeys(keys, s);
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
void ModeRunner::tickCustom(const Settings &s, const ChannelActions &ca, const RawAxes &raw, uint32_t buttons, bool fresh,
                            double dt) {
    custom_ = true;
    Wanted w = bindings(s, buttons);
    const bool nav_active = fresh && w.nav;

    const ChannelAction &av = ca.v, &ah = ca.h, &at = ca.twist, &al = ca.lift;
    auto eff = [](const ChannelAction &a, ChannelActionType dflt) {
        return a.type == ChannelActionType::Default ? dflt : a.type;
    };
    const ChannelActionType tv = eff(av, ChannelActionType::WheelV);
    const ChannelActionType th = eff(ah, ChannelActionType::WheelH);
    const ChannelActionType tt = eff(at, ChannelActionType::Zoom);
    const ChannelActionType tl = eff(al, ChannelActionType::Off);  // the lift does nothing unless asked to

    // A split channel runs its push and tilt parts as sub-channels (parent's settings + per-part
    // tuning, the part's share of the mix); the mixed channel then stays idle.
    const bool split[2] = {ca.vSplit.active(), ca.hSplit.active()};
    ChannelActionType part_t[PART_COUNT] = {tv, tv, th, th};
    const ChannelAction *part_a[PART_COUNT] = {&av, &av, &ah, &ah};
    double part_rate[PART_COUNT] = {0, 0, 0, 0};
    double rate_v = 0.0, rate_h = 0.0;
    if (split[0]) {
        part_a[PART_V_PUSH] = &ca.vSplit.pushAction(av);
        part_a[PART_V_TILT] = &ca.vSplit.tiltAction(av);
    }
    if (split[1]) {
        part_a[PART_H_PUSH] = &ca.hSplit.pushAction(ah);
        part_a[PART_H_TILT] = &ca.hSplit.tiltAction(ah);
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

    // Lift / press (LIFT_*): runs only while it has an action, so its filter and accumulator stay at rest otherwise.
    double rate_lift = 0.0;
    if (tl != ChannelActionType::Off) rate_lift = ch_lift_.process(s.lift, raw, dt);
    else ch_lift_.reset();

    // Twist with zoom keeps the dominance detection; all other actions are plain.
    double zoom_mixed = evalMix(s.zoom.mix, raw);
    double twist = std::fabs(zoom_mixed * s.zoom.sign);
    double lateral = lateralMax(s.zoom_lateral, raw, s.zoom.mix);
    if (nav_active || s.zoom.sensitivity <= 0) twist = 0.0;
    bool zooming = false;
    if (tt == ChannelActionType::Zoom) zooming = zoom_.update(twist, lateral, s, dt);
    else zoom_.reset();
    double rate_zoom = ch_zoom_.processValue(s.zoom, zoom_mixed, dt);

    // The key engine slots: what each motion runs (the type, the action and the value u it is fed with).
    struct Slot {
        ChannelActionType t = ChannelActionType::Off;
        const ChannelAction *a = nullptr;
        double rate = 0.0;
    } slot[KM_COUNT];
    for (int c = 0; c < 2; ++c) {
        if (split[c]) {
            for (int k = 0; k < 2; ++k) slot[c * 2 + k] = {part_t[c * 2 + k], part_a[c * 2 + k], part_rate[c * 2 + k]};
        } else {
            slot[c * 2] = {c == 0 ? tv : th, c == 0 ? &av : &ah, c == 0 ? rate_v : rate_h};
        }
    }
    slot[KM_TWIST] = {tt, &at, rate_zoom};
    slot[KM_LIFT] = {tl, &al, rate_lift};
    auto triggerOf = [&](int km) { return (s.key_tuning[km].set & KeyTuning::F_TRIGGER) ? s.key_tuning[km].trigger : s.step_trigger; };

    int nv = 0, nh = 0, nt = 0, nl = 0;
    int np[PART_COUNT] = {0, 0, 0, 0};
    if (zooming) {
        acc_v_.reset();
        acc_h_.reset();
        acc_lift_.reset();
        for (int i = 0; i < KM_COUNT; ++i) {
            rep_[i].reset();
            ptr_[i].reset();
            if (slot[i].t == ChannelActionType::Hold || (i == KM_LIFT && slot[i].t == ChannelActionType::Step)) {
                // A hold keeps following its motion while the twist zooms (it is released when the motion falls back). The
                // lift follows it too, its presses discarded: the zoom suppresses them, and a cap that is held beyond the
                // trigger when the zoom ends must not fire a step at that moment (the lift fires when the cap is grabbed).
                trig_[i].step(slot[i].rate, triggerOf(i));
            } else {
                trig_[i].reset();
            }
        }
        for (int i = 0; i < PART_COUNT; ++i) acc_part_[i].reset();
        nt = acc_zoom_.add(rate_zoom, dt);
    } else {
        // keys: runs the repeater and step: the digital trigger of the motion's key slot (key_engine.hpp); the wheels use the
        // accumulator. A slot that is not in use is kept at rest.
        auto channel = [&](ChannelActionType t, StepAccumulator &acc, int km, double rate) {
            if (t == ChannelActionType::Step || t == ChannelActionType::Hold) {
                acc.reset();
                rep_[km].reset();
                // A hold presses nothing: its state (trig_[km].state()) decides what is held below.
                const int n = trig_[km].step(rate, triggerOf(km));
                return t == ChannelActionType::Hold ? 0 : n;
            }
            trig_[km].reset();
            if (t == ChannelActionType::PointerX || t == ChannelActionType::PointerY) {
                acc.reset();
                rep_[km].reset();
                return ptr_[km].step(rate, dt, s.mouse_speed);  // pixels, not steps
            }
            ptr_[km].reset();
            if (t == ChannelActionType::Keys) {
                acc.reset();
                return rep_[km].step(rate, dt, keyParamsOf(s, km));
            }
            rep_[km].reset();
            if (t == ChannelActionType::Off) {
                acc.reset();
                return 0;
            }
            return stepOrReset(acc, rate, dt);
        };
        // The twist channel does not accumulate for zoom itself (the zoom detection does).
        nt = channel(tt == ChannelActionType::Zoom ? ChannelActionType::Off : tt, acc_zoom_, KM_TWIST, rate_zoom);
        if (split[0]) {
            for (int i = PART_V_PUSH; i <= PART_V_TILT; ++i) np[i] = channel(part_t[i], acc_part_[i], i, part_rate[i]);
        } else {
            nv = channel(tv, acc_v_, KM_V_PUSH, rate_v);
        }
        if (split[1]) {
            for (int i = PART_H_PUSH; i <= PART_H_TILT; ++i) np[i] = channel(part_t[i], acc_part_[i], i, part_rate[i]);
        } else {
            nh = channel(th, acc_h_, KM_H_PUSH, rate_h);
        }
        nl = channel(tl, acc_lift_, KM_LIFT, rate_lift);
    }

    std::bitset<kKeyCount> keys = w.keys;
    uint8_t mouse = w.mouse;
    // hold:<neg>,<pos>: the key, chord or mouse button of the deflected side is held; the union with the buttons' bindings and
    // with the zoom modifier below (ctrl stays down until every one of them lets go).
    for (int i = 0; i < KM_COUNT; ++i) {
        const bool isHold = slot[i].t == ChannelActionType::Hold;
        const int st = isHold ? trig_[i].state() : 0;
        if (st == 0) {
            hold_time_[i] = 0.0;
            hold_logged_[i] = false;
            continue;
        }
        hold_time_[i] += dt;
        if (hold_time_[i] > kHoldLogSeconds && !hold_logged_[i]) {
            hold_logged_[i] = true;  // logged once, not released: a long drag is legitimate
            std::cerr << LOG << "a hold: action has been held for more than 60 s (" << keyTuningPrefix(i)
                      << "), it is released when the motion falls back" << std::endl;
        }
        const ChannelAction &a = *slot[i].a;
        const bool pos = st > 0;
        if (pos ? a.posNone : a.negNone) continue;
        if (const uint8_t m = pos ? a.posMouse : a.negMouse) {
            mouse |= static_cast<uint8_t>(1u << m);
            continue;
        }
        const ChordMods &mods = pos ? a.posMods : a.negMods;
        for (uint8_t j = 0; j < mods.n; ++j) keys[idx(mods.k[j])] = true;
        keys[idx(pos ? a.pos : a.neg)] = true;
    }
    bool part_zoom = false;
    for (int i = 0; i < PART_COUNT; ++i)
        if (split[i / 2] && part_t[i] == ChannelActionType::Zoom && part_rate[i] != 0.0) part_zoom = true;
    if (tl == ChannelActionType::Zoom && rate_lift != 0.0) part_zoom = true;  // lift zooms: same exclusion as a part
    if (part_zoom) {
        // A push or tilt part zooms (for example "tilt forward = zoom"): the zoom modifier is held, so a mouse wheel of any other
        // motion would zoom too. While the part zooms the other wheel motions pause (their remainders are dropped), the same
        // exclusion the twist has through its dominance latch; keys and clicks are left as they are.
        auto wheel = [](ChannelActionType t) { return t == ChannelActionType::WheelV || t == ChannelActionType::WheelH; };
        for (int i = 0; i < PART_COUNT; ++i)
            if (split[i / 2] && wheel(part_t[i])) {
                np[i] = 0;
                acc_part_[i].reset();
            }
        if (!split[0] && wheel(tv)) { nv = 0; acc_v_.reset(); }
        if (!split[1] && wheel(th)) { nh = 0; acc_h_.reset(); }
        if (wheel(tt)) { nt = 0; acc_zoom_.reset(); }
        if (wheel(tl)) { nl = 0; acc_lift_.reset(); }
    }
    if (zooming || (!split[0] && tv == ChannelActionType::Zoom && rate_v != 0.0) ||
        (!split[1] && th == ChannelActionType::Zoom && rate_h != 0.0) || part_zoom)
        addZoomKeys(keys, s);
    syncMouse(mouse);
    syncKeys(keys);

    int pressBudget = kMaxPressesPerTick;  // key presses of all functions together in this tick
    int pointerDx = 0, pointerDy = 0;      // pixels of all pointer motions together in this tick (one moveMouse call)
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
        case ChannelActionType::Hold: break;  // held through the state above, never tapped
        case ChannelActionType::PointerX: pointerDx += n; break;  // positive: right
        case ChannelActionType::PointerY: pointerDy += n; break;  // positive: down
        case ChannelActionType::Step:
        case ChannelActionType::Keys:
            if (n > 0 ? a.posNone : a.negNone) break;  // "none" for this side: nothing is pressed
            if (const uint8_t m = n > 0 ? a.posMouse : a.negMouse) {
                // One click per press. A mouse button that is held by a binding or a hold is not clicked: its release would end
                // that drag.
                if ((mouse_held_ >> m) & 1) break;
                for (int i = std::abs(n); i > 0 && pressBudget > 0; --i, --pressBudget) {
                    out_->mouseButton(m, true);
                    out_->mouseButton(m, false);
                }
                break;
            }
            for (int i = std::abs(n); i > 0 && pressBudget > 0; --i, --pressBudget)
                tapChord(n > 0 ? a.pos : a.neg, n > 0 ? a.posMods : a.negMods);
            break;
        }
    };
    emit(av, tv, nv, 1);
    for (int i = PART_V_PUSH; i <= PART_V_TILT; ++i) emit(*part_a[i], part_t[i], np[i], 1);
    emit(ah, th, nh, 1);
    for (int i = PART_H_PUSH; i <= PART_H_TILT; ++i) emit(*part_a[i], part_t[i], np[i], 1);
    emit(at, tt, nt, -1);
    emit(al, tl, nl, 1);
    if (pointerDx != 0 || pointerDy != 0) out_->moveMouse(clampMouseDelta(pointerDx), clampMouseDelta(pointerDy));
    info_ = Info{rate_h, rate_v, twist, lateral, zooming, nav_active, rate_zoom, rate_lift};
    for (int i = 0; i < PART_COUNT; ++i) info_.rate_part[i] = split[i / 2] ? part_rate[i] : 0.0;
}

}  // namespace spn
