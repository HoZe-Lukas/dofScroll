// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The help of the visual joystick mode editor (issue #143, toolkit-free): it explains itself where you work
// and needs neither the web, nor hardware, nor the daemon.
//
//   motionSentence(form, base, motion, strings, live)   // "Tilt forward/back: presses Up or Down, about 3 times a
//                                                       //  second at full deflection"; with a live deflection
//                                                       //  "... now: Down, 2 per second"
//   helpTopic("motion:v_push", strings)                 // what it is, the axis, an example, the file key
//   motionTemplates(strings), applyTemplate(form, t)    // "Start from a template": fills the three channels
//   motionHints(form, base, deviceButtons, strings)     // hints that name the cause, each with its fix
//
// The rates come from the real channel processing (src/dsp.hpp) at the full deflection of the cap (350 raw
// units), so the sentence follows the sensitivity, dead zone, curve and sign of the mode and the profile.
#pragma once
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "dsp.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/fresh_state.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

constexpr double kFullDeflection = 350.0;  // raw units of a fully deflected cap

namespace help_detail {

// The channel processing a motion goes through, or false for lift / a motion no channel uses.
inline bool configOf(const ModeForm &form, const Settings &base, Motion m, ChannelCfg *out) {
    using namespace motion_map_detail;
    if (m == Motion::Lift) return false;
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) return false;
    const Settings s = effectiveSettings(form, base);
    const ChannelCfg &parent = cfgOf(s, p.channel);
    if (p.split && p.part) {
        Mix push, tilt;
        splitMix(parent.mix, &push, &tilt);
        const int idx = (p.channel == MotionChannel::V ? 0 : 2) + (isTilt(m) ? 1 : 0);
        *out = partConfig(parent, s.parts[idx], isTilt(m) ? tilt : push);
    } else {
        *out = parent;
    }
    return true;
}

// Steps per second (signed) the motion produces at the raw deflection `raw` held for a while.
inline double rateAt(const ChannelCfg &cfg, Axis axis, double raw) {
    RawAxes r{};
    r[axis] = int(std::lround(raw));
    Channel ch;
    double v = 0.0;
    for (int i = 0; i < 60; ++i) v = ch.process(cfg, r, 0.1);  // long enough for the smoothing to settle
    return v;
}

inline std::string number(double v) {
    const double a = std::fabs(v);
    if (a < 1.0) return "1";
    return std::to_string(long(std::lround(a)));
}

inline std::string keyName(const std::string &chord) { return motion_detail::keyWords(chord); }

}  // namespace help_detail

// One line in plain words for the selected motion. `liveRaw` (optional): the deflection of the motion's axis right
// now, signed raw units; NaN = static text only.
inline std::string motionSentence(const ModeForm &form, const Settings &base, Motion m, const StringProvider &s,
                                  double liveRaw = std::nan("")) {
    const MotionLabel l = motionLabel(form, base, m, s);
    if (m == Motion::Lift) {
        return l.lift ? format(s.tr("%1: is added to another motion; it is prone to false triggers when the cap is grabbed or released"), {l.name})
                      : format(s.tr("%1: not used"), {l.name});
    }
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) return format(s.tr("%1: does nothing here, no channel uses this motion (custom channel mix)"), {l.name});
    ChannelAction a;
    std::string err;
    const ChannelForm cf = actionOf(form, base, m);
    if (!parseChannelAction(cf.text(), &a, &err)) a = ChannelAction();
    ChannelCfg cfg;
    const bool haveCfg = help_detail::configOf(form, base, m, &cfg);
    const Axis axis = deriveAxisRoles(base).axis(m);  // placeOf found it, so the axis is known
    const bool live = !std::isnan(liveRaw);
    const double full = haveCfg ? std::fabs(help_detail::rateAt(cfg, axis, kFullDeflection)) : 0.0;
    const double now = haveCfg && live ? help_detail::rateAt(cfg, axis, liveRaw) : 0.0;
    const bool moving = live && std::fabs(now) >= 0.5;
    std::string text;
    switch (a.type) {
        case ChannelActionType::Default: text = s.tr("works as set in the profile (scroll and zoom as configured)"); break;
        case ChannelActionType::Off: text = s.tr("does nothing"); break;
        case ChannelActionType::WheelV:
            text = format(s.tr("scrolls up and down, about %1 notches a second at full deflection"), {help_detail::number(full)});
            if (moving) text += format(s.tr(", now: %1 a second"), {help_detail::number(now)});
            break;
        case ChannelActionType::WheelH:
            text = format(s.tr("scrolls sideways, about %1 notches a second at full deflection"), {help_detail::number(full)});
            if (moving) text += format(s.tr(", now: %1 a second"), {help_detail::number(now)});
            break;
        case ChannelActionType::Zoom:
            text = s.tr("zooms (mouse wheel with the zoom keys held)");
            if (moving) text += s.tr(", now: zooming");
            break;
        case ChannelActionType::Keys: {
            const std::string neg = help_detail::keyName(modelist::chordText(a.neg, a.negMods)),
                              pos = help_detail::keyName(modelist::chordText(a.pos, a.posMods));
            text = format(s.tr("presses %1 or %2, about %3 times a second at full deflection"), {neg, pos, help_detail::number(full)});
            if (moving) text += format(s.tr(", now: %1, %2 a second"), {now < 0 ? neg : pos, help_detail::number(now)});
            break;
        }
        default: {  // Step
            const std::string neg = help_detail::keyName(modelist::chordText(a.neg, a.negMods)),
                              pos = help_detail::keyName(modelist::chordText(a.pos, a.posMods));
            text = format(s.tr("taps %1 or %2 once per push"), {neg, pos});
            if (moving) text += format(s.tr(", now: %1"), {now < 0 ? neg : pos});
            break;
        }
    }
    if (live && !moving && a.type != ChannelActionType::Off) text += s.tr(", now: at rest");
    if (l.reversed) text += s.tr(" (direction reversed)");
    return l.name + ": " + text;
}

// "Button 3: Space" / "Button 3: not bound here, the profile's binding applies".
inline std::string buttonSentence(const ModeForm &form, int n, const StringProvider &s) {
    const std::string *act = form.button(n);
    if (!act) return format(s.tr("Button %1: not bound here, the profile's binding applies"), {std::to_string(n)});
    return format(s.tr("Button %1: %2"), {std::to_string(n), buttonActionWords(*act, s)});
}

struct HelpTopic {
    std::string title, body;
};

// The "?" popover of an element. Ids: "motion:<id>", "action:<type text>", "reverse", "tuning", "button", "picture".
// `roles` (optional): the axes of the configuration that is shown; without it the roles of the fresh state.
inline HelpTopic helpTopic(const std::string &id, const StringProvider &s, const AxisRoles *rolesIn = nullptr) {
    HelpTopic t;
    const AxisRoles fresh = rolesIn ? AxisRoles() : deriveAxisRoles(freshSettings());
    const AxisRoles &roles = rolesIn ? *rolesIn : fresh;
    auto motion = [&](Motion mo, const std::string &title, const std::string &how, const std::string &example, const std::string &key) {
        t.title = title;
        if (roles.has(mo))
            t.body = format(s.tr("%1 (axis %2). Example: %3. In the file: %4."), {how, axisName(roles.axis(mo)), example, key});
        else
            t.body = format(s.tr("%1 (this configuration assigns no axis to it). Example: %2. In the file: %3."), {how, example, key});
    };
    if (id == "motion:v_push")
        motion(Motion::VPush, s.tr("Vertical push"), s.tr("Pushing the cap away from you and toward you"), s.tr("scroll up and down"),
               s.tr("V_ACTION, or V_PUSH_ACTION when push and tilt have different actions"));
    else if (id == "motion:v_tilt")
        motion(Motion::VTilt, s.tr("Vertical tilt"), s.tr("Tilting the cap forward and back"), s.tr("press Page up and Page down"),
               s.tr("V_ACTION, or V_TILT_ACTION when push and tilt have different actions"));
    else if (id == "motion:h_push")
        motion(Motion::HPush, s.tr("Horizontal push"), s.tr("Pushing the cap to the left and to the right"), s.tr("scroll sideways"),
               s.tr("H_ACTION, or H_PUSH_ACTION when push and tilt have different actions"));
    else if (id == "motion:h_tilt")
        motion(Motion::HTilt, s.tr("Horizontal tilt"), s.tr("Tilting the cap to the left and to the right"), s.tr("press Left and Right"),
               s.tr("H_ACTION, or H_TILT_ACTION when push and tilt have different actions"));
    else if (id == "motion:twist")
        motion(Motion::Twist, s.tr("Twist"), s.tr("Twisting the cap around its vertical axis"), s.tr("zoom"), "TWIST_ACTION");
    else if (id == "motion:lift")
        motion(Motion::Lift, s.tr("Lift / press"), s.tr("Pulling the cap up or pressing it down"), s.tr("added to the vertical push as a mix term"),
               roles.has(Motion::VPush) && roles.has(Motion::VTilt) && roles.has(Motion::Lift)
                   ? format(s.tr("a term of a channel mix, for example %1"),
                            {std::string("SCROLL_V_MIX = ") + axisName(roles.axis(Motion::VPush)) + " + " + axisName(roles.axis(Motion::VTilt)) + " + " + axisName(roles.axis(Motion::Lift))})
                   : s.tr("a term of a channel mix"));
    else if (id == "action:default")
        t = {s.tr("Default behavior"), s.tr("The motion does what the profile says: scroll for push and tilt, zoom for the twist. In the file: default.")};
    else if (id == "action:none")
        t = {s.tr("Do nothing"), s.tr("The motion has no effect in this joystick mode. In the file: none.")};
    else if (id == "action:wheel_v")
        t = {s.tr("Scroll up and down"), s.tr("The motion turns the vertical mouse wheel; the harder you move, the faster it scrolls. In the file: wheel_v.")};
    else if (id == "action:wheel_h")
        t = {s.tr("Scroll sideways"), s.tr("The motion turns the horizontal mouse wheel. In the file: wheel_h.")};
    else if (id == "action:zoom")
        t = {s.tr("Zoom"), s.tr("The motion zooms: the mouse wheel is sent with the zoom keys held (ZOOM_MODIFIER, Ctrl by default). In the file: zoom.")};
    else if (id == "action:keys")
        t = {s.tr("Keys"), s.tr("The motion presses one key (or chord) for each direction; the harder you move, the faster it repeats. Example: keys:pageup,pagedown. In the file: keys:<negative>,<positive>.")};
    else if (id == "action:step")
        t = {s.tr("One key tap per push"), s.tr("One key press for each push or tilt, however far you go. Example: step:left,right. In the file: step:<negative>,<positive>.")};
    else if (id == "reverse")
        t = {s.tr("Reverse direction"), s.tr("Reverses the direction of this motion for scroll, zoom and keys alike. In the file: the sign of the channel, for example SCROLL_V_SIGN = -1.")};
    else if (id == "tuning")
        t = {s.tr("Tune this motion"), s.tr("Optional values that apply while this joystick mode is active: sensitivity, dead zone, curve and smoothing. An empty field follows the profile. In the file: for example SCROLL_V_SENS.")};
    else if (id == "button")
        t = {s.tr("Buttons"), s.tr("A button the joystick mode does not bind keeps the binding of the profile. A binding here applies only while this joystick mode is active. In the file: BUTTON_<n>.")};
    else if (id == "picture")
        t = {s.tr("Motions and buttons"), s.tr("The cap with its six motions and the buttons of the device. Choose one (click, or arrow keys and Enter) and set what it does on the right. Everything here is also in the Settings and Buttons tabs.")};
    return t;
}

// ---- templates ----------------------------------------------------------------------------------------

struct MotionTemplate {
    std::string id, title, description;
    ChannelForm v, h, twist;
};

inline ChannelForm templateKeys(ChannelActionType type, const char *neg, const char *pos) {
    ChannelForm c;
    c.type = type;
    c.neg = neg;
    c.pos = pos;
    return c;
}
inline ChannelForm templateType(ChannelActionType type) {
    ChannelForm c;
    c.type = type;
    return c;
}

inline std::vector<MotionTemplate> motionTemplates(const StringProvider &s) {
    using T = ChannelActionType;
    std::vector<MotionTemplate> out;
    out.push_back({"scroll", s.tr("Scroll and zoom"), s.tr("Push to scroll, twist to zoom, as in the profile."), templateType(T::Default),
                   templateType(T::Default), templateType(T::Default)});
    out.push_back({"pages", s.tr("Page up and down"), s.tr("Forward/back turns pages (Page up / Page down), sideways does nothing."),
                   templateKeys(T::Keys, "pageup", "pagedown"), templateType(T::Off), templateType(T::Off)});
    out.push_back({"arrows", s.tr("Arrow keys"), s.tr("Push or tilt in any direction to press the arrow keys."), templateKeys(T::Keys, "up", "down"),
                   templateKeys(T::Keys, "left", "right"), templateType(T::Off)});
    out.push_back({"timeline", s.tr("Timeline"), s.tr("Sideways scrubs with Left / Right, twist steps single frames."), templateType(T::Off),
                   templateKeys(T::Keys, "left", "right"), templateKeys(T::Step, "left", "right")});
    out.push_back({"zoompan", s.tr("Zoom and pan"), s.tr("Forward/back zooms, twist scrolls sideways."), templateType(T::Zoom), templateType(T::Default),
                   templateType(T::WheelH)});
    return out;
}

// Fills the three channels of the form from the template (push and tilt are no longer split); name, description,
// buttons and overrides stay.
inline void applyTemplate(ModeForm &form, const MotionTemplate &t) {
    form.v = t.v;
    form.h = t.h;
    form.twist = t.twist;
    form.vSplit = SplitForm();
    form.hSplit = SplitForm();
}

// ---- hints --------------------------------------------------------------------------------------------

struct MotionHint {
    std::string id;        // "off:<motion>", "keys:<a>:<b>", "mix:<channel key>", "button:<n>"
    std::string text;      // names the cause
    std::string fixLabel;  // "Assign it", "Show in Settings tab", "Unbind"
    std::string fixId;     // "select:<motion id>", "tab:<id>", "unbind:<n>"
};

namespace help_detail {
inline std::vector<std::string> chordsOf(const ChannelForm &c) {
    if (!c.usesKeys()) return {};
    return {c.neg, c.pos};
}
}  // namespace help_detail

inline std::vector<MotionHint> motionHints(const ModeForm &form, const Settings &base, int deviceButtons, const StringProvider &s) {
    using namespace motion_map_detail;
    std::vector<MotionHint> out;
    const std::vector<MotionLabel> labels = motionLabels(form, base, s);
    // a motion that does nothing
    for (const MotionLabel &l : labels) {
        if (l.motion == Motion::Lift) continue;
        const MotionPlace p = placeOf(form, base, l.motion);
        if (!p.found) continue;
        if (actionOf(form, base, l.motion).type == ChannelActionType::Off)
            out.push_back({std::string("off:") + motionId(l.motion), format(s.tr("%1 does nothing in this joystick mode"), {l.name}),
                           s.tr("Assign it"), std::string("select:") + motionId(l.motion)});
    }
    // two motions that press the same key
    for (size_t i = 0; i < labels.size(); ++i)
        for (size_t j = i + 1; j < labels.size(); ++j) {
            const Motion a = labels[i].motion, b = labels[j].motion;
            if (a == Motion::Lift || b == Motion::Lift) continue;
            const MotionPlace pa = placeOf(form, base, a), pb = placeOf(form, base, b);
            if (!pa.found || !pb.found) continue;
            if (pa.channel == pb.channel && !pa.split) continue;  // push and tilt of an unsplit channel are one action
            const std::vector<std::string> ka = help_detail::chordsOf(actionOf(form, base, a)), kb = help_detail::chordsOf(actionOf(form, base, b));
            std::string shared;
            for (const std::string &k : ka)
                if (!k.empty() && std::find(kb.begin(), kb.end(), k) != kb.end()) shared = k;
            if (!shared.empty())
                out.push_back({std::string("keys:") + motionId(a) + ":" + motionId(b),
                               format(s.tr("%1 and %2 both press %3"), {labels[i].name, labels[j].name, help_detail::keyName(shared)}),
                               format(s.tr("Show %1"), {labels[j].name}), std::string("select:") + motionId(b)});
        }
    // a channel mix of the mode that differs from the profile's
    const Settings eff = effectiveSettings(form, base);
    const AxisRoles roles = deriveAxisRoles(base);
    const struct { MotionChannel c; const char *name; } chans[] = {
        {MotionChannel::V, "Vertical"}, {MotionChannel::H, "Horizontal"}, {MotionChannel::Twist, "Twist"}};
    for (const auto &ch : chans) {
        if (!form.overrideValue(mixKey(ch.c))) continue;
        if (sameMix(withoutLift(mixOf(eff, ch.c), roles), withoutLift(mixOf(base, ch.c), roles))) continue;
        out.push_back({std::string("mix:") + mixKey(ch.c),
                       format(s.tr("This joystick mode replaces the channel mix of the profile (%1)"), {mixKey(ch.c)}),
                       s.tr("Show in Settings tab"), "tab:overrides"});
    }
    // a button the device does not have
    if (deviceButtons > 0)
        for (const auto &b : form.buttons)
            if (b.first > deviceButtons)
                out.push_back({"button:" + std::to_string(b.first),
                               format(s.tr("Button %1 is bound, but this device has only %2 buttons"), {std::to_string(b.first), std::to_string(deviceButtons)}),
                               s.tr("Unbind"), "unbind:" + std::to_string(b.first)});
    return out;
}

}  // namespace spn::ui
