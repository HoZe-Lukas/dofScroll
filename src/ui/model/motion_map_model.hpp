// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The six motions of the cap as the visual joystick mode editor sees them (issue #143, toolkit-free): the
// editor assigns an action to a motion and hides "channel" and "split". This model is the mapping from the
// six motions to the keys of a joystick mode file, on top of ModeForm (mode_editor_model.hpp), which keeps
// the storage: nothing here writes a file, the form is saved by ModeEditor as before.
//
// Motions and file keys (docs/modes.md):
//   VPush  z   V_ACTION, or V_PUSH_ACTION when split     VTilt  rx  V_ACTION, or V_TILT_ACTION when split
//   HPush  x   H_ACTION, or H_PUSH_ACTION when split     HTilt  rz  H_ACTION, or H_TILT_ACTION when split
//   Twist  ry  TWIST_ACTION                              Lift   y   no key of its own: a term of a channel mix
//
// Push and tilt of one channel with the same action are written as the unsplit key, different actions as
// the split keys. A hand-written channel mix is never changed by assigning an action; it is reported
// (MotionPlace::customMix) so the view can say so. Lift/press is assigned through the mix of a channel
// (setLift) and is never part of what ships (tests/test_no_lift_press.cpp).
//
//   actionOf(form, settings, motion)               // the action the mode gives the motion now
//   assign(form, settings, motion, action)         // collapses or splits the channel as needed
//   isReversed / setReversed(form, settings, m, on)  // the existing *_SIGN keys (no new key)
//   liftPlace / setLift(form, settings, channel, weight)
#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

enum class Motion { VPush, VTilt, HPush, HTilt, Twist, Lift };
enum class MotionChannel { V, H, Twist };

inline const std::vector<Motion> &allMotions() {
    static const std::vector<Motion> m = {Motion::VPush, Motion::VTilt, Motion::HPush, Motion::HTilt, Motion::Twist, Motion::Lift};
    return m;
}

// "v_push", ... as in motion_summary_model.hpp.
inline const char *motionId(Motion m) {
    switch (m) {
        case Motion::VPush: return "v_push";
        case Motion::VTilt: return "v_tilt";
        case Motion::HPush: return "h_push";
        case Motion::HTilt: return "h_tilt";
        case Motion::Twist: return "twist";
        case Motion::Lift: return "lift";
    }
    return "v_push";
}
inline bool motionFromId(const std::string &id, Motion *out) {
    for (Motion m : allMotions())
        if (id == motionId(m)) {
            *out = m;
            return true;
        }
    return false;
}
inline Axis motionAxis(Motion m) {
    switch (m) {
        case Motion::VPush: return AX_Z;
        case Motion::VTilt: return AX_RX;
        case Motion::HPush: return AX_X;
        case Motion::HTilt: return AX_RZ;
        case Motion::Twist: return AX_RY;
        case Motion::Lift: return AX_Y;
    }
    return AX_Z;
}
// The motion of a raw axis.
inline Motion motionOfAxis(Axis a) {
    for (Motion m : allMotions())
        if (motionAxis(m) == a) return m;
    return Motion::VPush;
}
// The channel the motion belongs to in the built-in mixes (V = z + rx, H = x - rz, twist = ry); lift has none.
inline bool naturalChannel(Motion m, MotionChannel *out) {
    switch (m) {
        case Motion::VPush:
        case Motion::VTilt: *out = MotionChannel::V; return true;
        case Motion::HPush:
        case Motion::HTilt: *out = MotionChannel::H; return true;
        case Motion::Twist: *out = MotionChannel::Twist; return true;
        case Motion::Lift: return false;
    }
    return false;
}
inline bool isTilt(Motion m) { return m == Motion::VTilt || m == Motion::HTilt; }

namespace motion_map_detail {

inline const char *mixKey(MotionChannel c) { return c == MotionChannel::V ? "SCROLL_V_MIX" : c == MotionChannel::H ? "SCROLL_H_MIX" : "ZOOM_MIX"; }

inline const Mix &mixOf(const Settings &s, MotionChannel c) {
    return c == MotionChannel::V ? s.scroll_v.mix : c == MotionChannel::H ? s.scroll_h.mix : s.zoom.mix;
}
inline const ChannelCfg &cfgOf(const Settings &s, MotionChannel c) {
    return c == MotionChannel::V ? s.scroll_v : c == MotionChannel::H ? s.scroll_h : s.zoom;
}

inline bool hasAxis(const Mix &m, Axis a) {
    for (const Term &t : m.terms)
        if (t.axis == a && t.weight != 0.0) return true;
    return false;
}

inline Mix withoutAxis(const Mix &m, Axis a) {
    Mix out;
    for (const Term &t : m.terms)
        if (t.axis != a) out.terms.push_back(t);
    return out;
}

inline ChannelForm &channelForm(ModeForm &f, MotionChannel c) { return c == MotionChannel::V ? f.v : c == MotionChannel::H ? f.h : f.twist; }
inline const ChannelForm &channelForm(const ModeForm &f, MotionChannel c) { return c == MotionChannel::V ? f.v : c == MotionChannel::H ? f.h : f.twist; }
inline SplitForm *splitForm(ModeForm &f, MotionChannel c) { return c == MotionChannel::V ? &f.vSplit : c == MotionChannel::H ? &f.hSplit : nullptr; }
inline const SplitForm *splitForm(const ModeForm &f, MotionChannel c) { return c == MotionChannel::V ? &f.vSplit : c == MotionChannel::H ? &f.hSplit : nullptr; }

inline bool sameMix(const Mix &a, const Mix &b) { return mixToString(a) == mixToString(b); }

inline std::string signKey(MotionChannel c, bool split, bool tilt) {
    if (c == MotionChannel::Twist) return "ZOOM_SIGN";
    const std::string ch = c == MotionChannel::V ? "SCROLL_V_" : "SCROLL_H_";
    return split ? ch + (tilt ? "TILT_SIGN" : "PUSH_SIGN") : ch + "SIGN";
}

}  // namespace motion_map_detail

// `base` with the overrides of the form applied (the channel mixes and signs the mode really runs with).
inline Settings effectiveSettings(const ModeForm &f, Settings base) {
    for (const auto &kv : f.overrides) applyKey(base, utf8::upperAscii(kv.first), kv.second, nullptr);
    return base;
}

// Where a motion lives in the mode: the first channel (V, H, twist) whose mix has a term with its axis (a
// split channel drops the twist term), as motion_summary_model.hpp does.
struct MotionPlace {
    bool found = false;
    MotionChannel channel = MotionChannel::V;
    bool part = false;       // true: the motion is the push or tilt part of a V or H channel
    bool customMix = false;  // the mix of that channel (lift terms aside) is not the built-in one, or the motion is in no mix
    bool split = false;      // the channel is split now
};

inline MotionPlace placeOf(const ModeForm &form, const Settings &base, Motion m) {
    using namespace motion_map_detail;
    const Settings s = effectiveSettings(form, base);
    const Settings builtin;
    MotionPlace p;
    const Axis ax = motionAxis(m);
    const MotionChannel order[3] = {MotionChannel::V, MotionChannel::H, MotionChannel::Twist};
    for (MotionChannel c : order) {
        const SplitForm *sp = splitForm(form, c);
        const bool split = sp && sp->active();
        if (split && ax == AX_RY) continue;
        if (!hasAxis(mixOf(s, c), ax)) continue;
        p.found = true;
        p.channel = c;
        p.split = split;
        p.part = c != MotionChannel::Twist && ax != AX_RY;
        break;
    }
    MotionChannel nat;
    if (!naturalChannel(m, &nat)) return p;  // lift: no built-in place
    if (!p.found || p.channel != nat) {
        p.customMix = true;
        return p;
    }
    p.customMix = !sameMix(withoutAxis(mixOf(s, p.channel), AX_Y), withoutAxis(mixOf(builtin, p.channel), AX_Y));
    return p;
}

// The action the mode gives a motion (for an unused or lift motion: the default of the channel it would use,
// "default"). `found` (optional) tells whether the motion is part of a mix at all.
inline ChannelForm actionOf(const ModeForm &form, const Settings &base, Motion m, bool *found = nullptr) {
    using namespace motion_map_detail;
    const MotionPlace p = placeOf(form, base, m);
    if (found) *found = p.found;
    if (!p.found) return ChannelForm();
    const ChannelForm &ch = channelForm(form, p.channel);
    const SplitForm *sp = splitForm(form, p.channel);
    if (!p.split || !sp || !p.part) return ch;
    if (isTilt(m)) return sp->tiltSet ? sp->tilt : ch;
    return sp->pushSet ? sp->push : ch;
}

// The ChannelForm inside the form that holds the motion's action (the channel's own, or a split part's), so a view
// can edit its keys in place; nullptr when the motion is in no channel mix or is lift. A part that follows its
// channel gives the channel's form.
inline ChannelForm *actionRef(ModeForm &form, const Settings &base, Motion m) {
    using namespace motion_map_detail;
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found || m == Motion::Lift) return nullptr;
    SplitForm *sp = splitForm(form, p.channel);
    if (!p.split || !sp || !p.part) return &channelForm(form, p.channel);
    if (isTilt(m)) return sp->tiltSet ? &sp->tilt : &channelForm(form, p.channel);
    return sp->pushSet ? &sp->push : &channelForm(form, p.channel);
}

struct AssignResult {
    bool ok = false;
    std::string error;  // English source text
};

// Gives the motion the action. Same action as now: nothing changes. Twist: TWIST_ACTION. Push or tilt:
// when the other part of the channel has the same action afterwards the channel is written unsplit
// (V_ACTION / H_ACTION, both split keys removed), otherwise split (the other part keeps what it did).
// Settings of a part (SCROLL_V_PUSH_SENS ...) stay in the file; they only act while the channel is split.
// Not for lift (setLift) and not for a motion that is in no channel mix.
inline AssignResult assign(ModeForm &form, const Settings &base, Motion m, const ChannelForm &a) {
    using namespace motion_map_detail;
    AssignResult r;
    if (m == Motion::Lift) {
        r.error = "lift / press has no action of its own; it can only be added to another motion";
        return r;
    }
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) {
        r.error = "this motion is not used by any channel of this joystick mode (custom channel mix)";
        return r;
    }
    if (actionOf(form, base, m) == a) {
        r.ok = true;
        return r;
    }
    if (!p.part) {
        channelForm(form, p.channel) = a;
        r.ok = true;
        return r;
    }
    SplitForm &sp = *splitForm(form, p.channel);
    ChannelForm &ch = channelForm(form, p.channel);
    const bool tilt = isTilt(m);
    const ChannelForm other = actionOf(form, base, tilt ? (p.channel == MotionChannel::V ? Motion::VPush : Motion::HPush)
                                                         : (p.channel == MotionChannel::V ? Motion::VTilt : Motion::HTilt));
    if (other == a) {  // both parts end up the same: the unsplit key
        ch = a;
        sp = SplitForm();
        r.ok = true;
        return r;
    }
    if (!sp.active()) sp.setSplit(true, ch);
    (tilt ? sp.tilt : sp.push) = a;
    (tilt ? sp.tiltSet : sp.pushSet) = true;
    // the other part keeps its action; when only this part was set before, pin the other one to what it did
    (tilt ? sp.push : sp.tilt) = other;
    (tilt ? sp.pushSet : sp.tiltSet) = true;
    r.ok = true;
    return r;
}

// "Reverse direction": the sign of the motion (SCROLL_V_SIGN, SCROLL_H_SIGN, ZOOM_SIGN, or the sign of the
// part while the channel is split). Lift has no sign of its own: false.
inline bool isReversed(const ModeForm &form, const Settings &base, Motion m) {
    using namespace motion_map_detail;
    if (m == Motion::Lift) return false;
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) return false;
    const Settings s = effectiveSettings(form, base);
    if (p.split && p.part) {
        const std::string *v = form.overrideValue(signKey(p.channel, true, isTilt(m)));
        if (v) return std::atoi(v->c_str()) < 0;
    }
    return cfgOf(s, p.channel).sign < 0;
}

// Sets the direction; writes the sign only when it differs from what the part or channel would have without
// the key, so a mode that does not reverse anything has no sign line.
inline AssignResult setReversed(ModeForm &form, const Settings &base, Motion m, bool reversed) {
    using namespace motion_map_detail;
    AssignResult r;
    if (m == Motion::Lift) {
        r.error = "lift / press has no direction of its own";
        return r;
    }
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) {
        r.error = "this motion is not used by any channel of this joystick mode (custom channel mix)";
        return r;
    }
    const int want = reversed ? -1 : 1;
    const bool partKey = p.split && p.part;
    const std::string key = signKey(p.channel, partKey, isTilt(m));
    ModeForm without = form;
    without.setOverride(key, "");
    const int follow = cfgOf(effectiveSettings(without, base), p.channel).sign;
    form.setOverride(key, want == follow ? std::string() : std::to_string(want));
    r.ok = true;
    return r;
}

// Which channel a lift/press term is in now (the first mix with an y term); found = false when none.
inline MotionPlace liftPlace(const ModeForm &form, const Settings &base) {
    return placeOf(form, base, Motion::Lift);
}

// Adds lift/press to a channel's mix with `weight` (a signed number), or removes it from every mix with
// weight 0. The mix is written as an override of the mode (SCROLL_V_MIX, SCROLL_H_MIX, ZOOM_MIX); an override
// that equals the built-in mix is removed again. The view says in words that this is prone to false triggers.
inline AssignResult setLift(ModeForm &form, const Settings &base, MotionChannel target, double weight) {
    using namespace motion_map_detail;
    AssignResult r;
    if (!std::isfinite(weight) || std::fabs(weight) > 100.0) {
        r.error = "the weight must be a number between -100 and 100";
        return r;
    }
    const MotionChannel order[3] = {MotionChannel::V, MotionChannel::H, MotionChannel::Twist};
    const Settings eff = effectiveSettings(form, base);
    for (MotionChannel c : order) {
        if (c != target && !hasAxis(mixOf(eff, c), AX_Y)) continue;  // other mixes stay exactly as they are
        Mix mix = withoutAxis(mixOf(eff, c), AX_Y);
        if (c == target && weight != 0.0) mix.terms.push_back({AX_Y, weight});
        if (mix.terms.size() > kMaxMixTerms) {
            r.error = "the channel mix would have too many terms";
            return r;
        }
        form.setOverride(mixKey(c), sameMix(mix, mixOf(base, c)) ? std::string() : mixToString(mix));
    }
    r.ok = true;
    return r;
}

// What the picture and the list say about a motion: the name, how to move, the action in words and a symbol
// (the same words as motion_summary_model.hpp), computed from the form so it follows every edit.
struct MotionLabel {
    Motion motion = Motion::VPush;
    std::string name, how;
    std::string action;   // "Scroll", "Keys: Up / Down", "Off", "Not used"
    std::string chip;     // "Scroll", "Keys", "Taps", ...
    std::string symbol;   // icon key as in motion_summary_model.hpp
    MotionKind kind = MotionKind::Active;
    bool customMix = false;
    bool lift = false;       // lift/press is part of a channel mix (only for Motion::Lift)
    bool reversed = false;
    std::string badge;       // "Custom channel mix", "Not used: custom channel mix", "" = none
};

inline MotionLabel motionLabel(const ModeForm &form, const Settings &base, Motion m, const StringProvider &s) {
    // the names are literal so that the translation extraction finds them
    struct Names { std::string name, how; };
    Names n;
    switch (m) {
        case Motion::VPush: n = {s.tr("Vertical push"), s.tr("Push forward / back")}; break;
        case Motion::VTilt: n = {s.tr("Vertical tilt"), s.tr("Tilt forward / back")}; break;
        case Motion::HPush: n = {s.tr("Horizontal push"), s.tr("Push sideways")}; break;
        case Motion::HTilt: n = {s.tr("Horizontal tilt"), s.tr("Tilt sideways")}; break;
        case Motion::Twist: n = {s.tr("Twist"), s.tr("Twist the cap")}; break;
        case Motion::Lift: n = {s.tr("Lift / press"), s.tr("Push down / pull up")}; break;
    }
    const MotionPlace p = placeOf(form, base, m);
    MotionRow row;
    if (!p.found) {
        row = motion_detail::unusedRow(motionId(m), n.name, n.how, s);
    } else {
        ChannelAction a;
        std::string err;
        const ChannelForm cf = actionOf(form, base, m);
        if (!parseChannelAction(cf.text(), &a, &err)) a = ChannelAction();
        row = motion_detail::rowFor(motionId(m), n.name, n.how, a, p.channel == MotionChannel::V, p.channel == MotionChannel::Twist,
                                    effectiveSettings(form, base).zoom_modifier, s);
    }
    MotionLabel l;
    l.motion = m;
    l.name = row.name;
    l.how = row.how;
    l.action = row.action;
    l.chip = row.chip;
    l.symbol = row.symbol;
    l.kind = row.kind;
    l.customMix = p.customMix;
    l.lift = m == Motion::Lift && p.found;
    l.reversed = isReversed(form, base, m);
    if (m == Motion::Lift) {
        if (p.found) {
            l.action = s.tr("Added to another motion");
            l.symbol = "lift_in";
            l.kind = MotionKind::Active;
        }
    } else if (p.customMix) {
        l.badge = p.found ? s.tr("Custom channel mix") : s.tr("Not used: custom channel mix");
    }
    return l;
}

inline std::vector<MotionLabel> motionLabels(const ModeForm &form, const Settings &base, const StringProvider &s) {
    std::vector<MotionLabel> out;
    for (Motion m : allMotions()) out.push_back(motionLabel(form, base, m, s));
    return out;
}

}  // namespace spn::ui
