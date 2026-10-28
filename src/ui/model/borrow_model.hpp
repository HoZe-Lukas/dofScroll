// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Take over from ...": copies the assignment of one motion or one button from another profile, a joystick mode or a
// button mode into the profile being edited (toolkit-free). It is a COPY: the EFFECTIVE assignment of the source (what
// it really does, all layers and its joystick mode merged) is written as explicit keys of the target profile and then
// belongs to it; later changes in the source do not reach it, and the profile stays editable like any other.
//
//   motionSources(model, target)                       // other profiles, joystick modes (what can be copied from)
//   buttonSources(model, target, &buttonModes)         // the same plus button modes
//   borrowMotion(model, target, "v_tilt", source, &err)           // push and tilt are copied separately
//   borrowMotion(model, target, "v_tilt", source, &err, "twist")  // the action of another motion of the source
//   borrowButton(model, target, 3, source, &err)
//   borrowedMotionWords(...) / borrowedButtonWords(...)           // what would be copied, in words (for the dialog)
//
// Motions: by default only the ACTION of the motion is copied (V_PUSH_ACTION ... TWIST_ACTION of the target); the
// tuning of its channel (sensitivity, dead zone, direction, curve) stays as the target has it, and the source's channel
// keys are not touched. With "also take over the tuning" (tuning = true) copyMotionSettings() also writes the EFFECTIVE
// tuning of the source motion as explicit keys: SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS> (ZOOM_*
// for the twist); a motion that is tuned off in the source (sensitivity 0) is copied as an explicit SENS = 0. The one
// function serves profiles (borrowMotion) and joystick modes (borrowMotionIntoMode). A motion the source has switched off (action none, or sensitivity 0) is copied as "cleared" (none).
// Buttons: the effective binding of the source (a joystick mode's binding beats Global and device, a program's own
// binding beats the mode); nothing bound, or cleared, is copied as an explicit "nothing". A button mode is copied as
// its action (a button mode with a cycle list keeps its list); to follow a button mode by name use the profile
// builder or the button's own list.
// Writes go through the SettingsModel and motion_edit_model.hpp, so an invalid or impossible value is refused with an
// error text and nothing is written (a profile below its joystick mode cannot beat the mode, see setMotionAction).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "button_modes.hpp"
#include "profile_actions.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

struct BorrowSource {
    enum class Kind { Profile, JoystickMode, ButtonMode };
    Kind kind = Kind::Profile;
    size_t section = 0;  // Profile: the section index
    std::string id;      // JoystickMode / ButtonMode: the id
    std::string label;   // English source text for lists: "Global", "Device profile: Pro", "Program profile: code", "Joystick mode: Timeline"
};

namespace borrow_detail {
inline std::string profileLabel(const SettingsModel &m, size_t i) {
    const SettingsModel::SectionInfo info = m.sections()[i];
    if (info.kind == Section::GLOBAL) return "Global";
    return std::string(info.kind == Section::DEVICE ? "Device profile: " : "Program profile: ") + info.pattern;
}
inline std::string modeName(const ModeDef &d) { return d.label.empty() ? d.id : d.label; }
inline bool motionIdOk(const std::string &id) {
    return id == "v_push" || id == "v_tilt" || id == "h_push" || id == "h_tilt" || id == "twist";
}
// The action of one motion in a joystick mode alone.
inline ChannelAction modeAction(const ModeDef &d, const std::string &id) {
    const ChannelActions ca = effectiveChannelActions(d, Settings());
    if (id == "twist") return ca.twist;
    const bool v = id[0] == 'v';
    const SplitChannel &sp = v ? ca.vSplit : ca.hSplit;
    const ChannelAction &parent = v ? ca.v : ca.h;
    return id.find("tilt") != std::string::npos ? sp.tiltAction(parent) : sp.pushAction(parent);
}
inline std::string actionWords(const ChannelAction &a, bool off, const StringProvider &s) {
    if (off || a.type == ChannelActionType::Off) return s.tr("nothing (cleared)");
    switch (a.type) {
        case ChannelActionType::Default: return s.tr("the normal action (scroll, or zoom for the twist)");
        case ChannelActionType::WheelV: return s.tr("scroll");
        case ChannelActionType::WheelH: return s.tr("scroll sideways");
        case ChannelActionType::Zoom: return s.tr("zoom");
        case ChannelActionType::Keys: return format(s.tr("keys %1 / %2"), {modelist::negChordText(a), modelist::posChordText(a)});
        case ChannelActionType::Step: return format(s.tr("one key tap per push: %1 / %2"), {modelist::negChordText(a), modelist::posChordText(a)});
        case ChannelActionType::Off: break;
    }
    return std::string();
}
}  // namespace borrow_detail

// target: the section being edited (left out), or ProfileSet::npos; excludeMode: the joystick mode being edited (left out).
inline std::vector<BorrowSource> motionSources(const SettingsModel &m, size_t target, const std::string &excludeMode = std::string()) {
    std::vector<BorrowSource> out;
    for (size_t i = 0; i < m.sectionCount(); ++i) {
        if (i == target) continue;
        BorrowSource b;
        b.section = i;
        b.label = borrow_detail::profileLabel(m, i);
        out.push_back(b);
    }
    for (const SettingsModel::ModeOption &o : m.modeOptions(0)) {
        if (o.id.empty() || !o.known || !m.modeDef(o.id) || o.id == excludeMode) continue;
        BorrowSource b;
        b.kind = BorrowSource::Kind::JoystickMode;
        b.id = o.id;
        b.label = "Joystick mode: " + o.label;
        out.push_back(b);
    }
    return out;
}

inline std::vector<BorrowSource> buttonSources(const SettingsModel &m, size_t target, const ButtonModeSet *buttonModes,
                                               const std::string &excludeMode = std::string()) {
    std::vector<BorrowSource> out = motionSources(m, target, excludeMode);
    if (buttonModes)
        for (const ButtonMode &b : buttonModes->modes) {
            BorrowSource s;
            s.kind = BorrowSource::Kind::ButtonMode;
            s.id = b.id;
            s.label = "Button mode: " + (b.label.empty() ? b.id : b.label);
            out.push_back(s);
        }
    return out;
}

// What the source does with the motion, effective: the action and whether the motion is off there.
struct BorrowedMotion {
    bool ok = false;
    ChannelAction action;
    bool off = false;
    std::string label;  // of the source, English
    std::string error;
};

inline BorrowedMotion borrowedMotion(const SettingsModel &m, const BorrowSource &src, const std::string &motionId) {
    BorrowedMotion r;
    if (!borrow_detail::motionIdOk(motionId)) {
        r.error = "lift / press has no action of its own";
        return r;
    }
    if (src.kind == BorrowSource::Kind::Profile) {
        if (src.section >= m.sectionCount()) {
            r.error = "no such profile";
            return r;
        }
        r.action = motion_edit_detail::effectiveAction(m, src.section, motionId);
        r.off = !motion_edit_detail::effectiveOn(m, src.section, motionId);
        r.label = borrow_detail::profileLabel(m, src.section);
    } else if (src.kind == BorrowSource::Kind::JoystickMode) {
        const ModeDef *d = m.modeDef(src.id);
        if (!d) {
            r.error = "no such joystick mode";
            return r;
        }
        r.action = borrow_detail::modeAction(*d, motionId);
        r.off = r.action.type == ChannelActionType::Off;
        r.label = "Joystick mode: " + borrow_detail::modeName(*d);
    } else {
        r.error = "a button mode has no motions";
        return r;
    }
    r.ok = true;
    return r;
}

// ---- the tuning of a motion: SCROLL_<V|H>_<PUSH|TILT>_<...>, ZOOM_<...> (the one shared copy) ----
namespace borrow_detail {
inline const char *const kTuningFields[] = {"SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"};
// The settings the source really runs with: a profile with its joystick mode merged; a joystick mode on top of Global.
inline bool sourceSettings(const SettingsModel &m, const BorrowSource &src, Settings *out, std::string *err) {
    if (src.kind == BorrowSource::Kind::Profile) {
        if (src.section >= m.sectionCount()) {
            if (err) *err = "no such profile";
            return false;
        }
        *out = m.effectiveWithMode(src.section);
        return true;
    }
    const ModeDef *d = src.kind == BorrowSource::Kind::JoystickMode ? m.modeDef(src.id) : nullptr;
    if (!d) {
        if (err) *err = src.kind == BorrowSource::Kind::JoystickMode ? "no such joystick mode" : "a button mode has no motions";
        return false;
    }
    *out = m.effective(0);
    std::vector<KV> ov;
    for (const auto &kv : d->overrides) ov.emplace_back(utf8::upperAscii(kv.first), kv.second);
    applyKvList(*out, ov, 0);
    return true;
}
inline std::string tuningPrefix(const std::string &id) {
    if (id == "twist") return "ZOOM_";
    return std::string(id[0] == 'v' ? "SCROLL_V_" : "SCROLL_H_") + (id.find("tilt") != std::string::npos ? "TILT_" : "PUSH_");
}
}  // namespace borrow_detail

// The effective tuning of one motion of the source as explicit key / value pairs in the canonical config form (five keys).
inline bool motionTuning(const SettingsModel &m, const BorrowSource &src, const std::string &motionId, std::vector<KV> *out, std::string *err) {
    if (!borrow_detail::motionIdOk(motionId)) {
        if (err) *err = "lift / press has no action of its own";
        return false;
    }
    Settings s;
    if (!borrow_detail::sourceSettings(m, src, &s, err)) return false;
    const std::string pre = borrow_detail::tuningPrefix(motionId);
    Settings tmp;  // carries the values, so the canonical text comes from the one writer of the config
    if (motionId == "twist") {
        tmp.zoom = s.zoom;
    } else {
        const bool v = motionId[0] == 'v', tilt = motionId.find("tilt") != std::string::npos;
        const int part = (v ? 0 : 2) + (tilt ? 1 : 0);
        PartTuning t;
        t.set = PartTuning::F_SENS | PartTuning::F_DEADZONE | PartTuning::F_SIGN | PartTuning::F_CURVE | PartTuning::F_SMOOTH;
        t.cfg = partConfig(v ? s.scroll_v : s.scroll_h, s.parts[part], Mix());
        tmp.parts[part] = t;
    }
    const std::vector<KV> all = allKeys(tmp);
    out->clear();
    for (const char *f : borrow_detail::kTuningFields)
        for (const KV &kv : all)
            if (kv.first == pre + f) out->emplace_back(kv);
    return out->size() == 5;
}

// Where the copied tuning goes: a profile (SettingsModel::setValue) or a joystick mode form (ModeForm::setOverride).
using TuningSink = std::function<bool(const std::string &key, const std::string &value, std::string *err)>;

// THE copy of the tuning of the motion `fromMotion` (default: the same) of the source into the motion `motionId` of the
// destination: the five keys of the destination's motion, explicit. Every value is checked first.
inline bool copyMotionSettings(const SettingsModel &m, const BorrowSource &src, const TuningSink &dst, const std::string &motionId,
                               std::string *err, const std::string &fromMotion = std::string()) {
    const std::string fromId = fromMotion.empty() ? motionId : fromMotion;
    std::vector<KV> from;
    if (!borrow_detail::motionIdOk(motionId)) {
        if (err) *err = "lift / press has no action of its own";
        return false;
    }
    if (!motionTuning(m, src, fromId, &from, err)) return false;
    const std::string fromPre = borrow_detail::tuningPrefix(fromId);
    const std::string toPre = borrow_detail::tuningPrefix(motionId);
    std::vector<KV> to;
    for (const KV &kv : from) to.emplace_back(toPre + kv.first.substr(fromPre.size()), kv.second);
    Settings probe;
    for (const KV &kv : to)
        if (!applyKey(probe, kv.first, kv.second, err)) return false;
    for (const KV &kv : to)
        if (!dst(kv.first, kv.second, err)) return false;
    return true;
}

// The tuning of the source motion in words, for the dialog: "sensitivity 7, dead zone 30, direction normal, ...".
inline std::string motionTuningWords(const SettingsModel &m, const BorrowSource &src, const std::string &motionId, const StringProvider &s) {
    std::vector<KV> kv;
    if (!motionTuning(m, src, motionId, &kv, nullptr)) return std::string();
    const std::string pre = borrow_detail::tuningPrefix(motionId);
    auto val = [&](const char *f) {
        for (const KV &p : kv)
            if (p.first == pre + f) return p.second;
        return std::string();
    };
    return format(s.tr("sensitivity %1, dead zone %2, direction %3, curve %4, smoothing %5 ms"),
                  {val("SENS"), val("DEADZONE"), val("SIGN") == "-1" ? s.tr("reversed") : s.tr("normal"), val("CURVE"), val("SMOOTH_MS")});
}

// The label of a source in the user's language: the English prefix ("Device profile: Pro") is translated, the name stays.
inline std::string sourceLabel(const std::string &label, const StringProvider &s) {
    const size_t c = label.find(": ");
    if (c == std::string::npos) return label == "Global" ? s.tr("Global") : label;
    const std::string pre = label.substr(0, c), name = label.substr(c + 2);
    const std::string t = pre == "Device profile" ? s.tr("Device profile")
                          : pre == "Program profile" ? s.tr("Program profile")
                          : pre == "Joystick mode" ? s.tr("Joystick mode")
                          : pre == "Button mode" ? s.tr("Button mode")
                                                 : pre;
    return t + ": " + name;
}

// One sentence for the dialog: what would be copied.
inline std::string borrowedMotionWords(const SettingsModel &m, const BorrowSource &src, const std::string &motionId, const StringProvider &s,
                                       bool tuning = false) {
    const BorrowedMotion b = borrowedMotion(m, src, motionId);
    if (!b.ok) return s.tr(b.error.c_str());
    if (tuning)
        return format(s.tr("%1 does: %2. Taking it over copies this action and its tuning (%3) into the profile; later changes in the source do not follow."),
                      {sourceLabel(b.label, s), borrow_detail::actionWords(b.action, b.off, s), motionTuningWords(m, src, motionId, s)});
    return format(s.tr("%1 does: %2. Taking it over copies this action into the profile; later changes in the source do not follow. The tuning (sensitivity, direction) stays as it is here."),
                  {sourceLabel(b.label, s),
                   borrow_detail::actionWords(b.action, b.off, s)});
}

// Copies the effective action of `fromMotion` of the source (default: the same motion) into `motionId` of the target.
inline bool borrowMotion(SettingsModel &m, size_t target, const std::string &motionId, const BorrowSource &src, std::string *err,
                         const std::string &fromMotion = std::string(), bool tuning = false) {
    auto fail = [&](const std::string &e) {
        if (err) *err = e;
        return false;
    };
    if (target >= m.sectionCount()) return fail("no such profile");
    if (!borrow_detail::motionIdOk(motionId)) return fail("lift / press has no action of its own");
    if (src.kind == BorrowSource::Kind::Profile && src.section == target) return fail("take it over from another profile");
    const BorrowedMotion b = borrowedMotion(m, src, fromMotion.empty() ? motionId : fromMotion);
    if (!b.ok) return fail(b.error);
    ChannelForm form = channelFormOf(b.action);
    if (b.off) form = ChannelForm(), form.type = ChannelActionType::Off;
    if (!tuning) return setMotionAction(m, target, motionId, form, err);
    const SettingsModel backup = m;  // the action is written first (it can be refused), then the tuning (checked before it is written)
    if (!setMotionAction(m, target, motionId, form, err)) return false;
    const TuningSink sink = [&](const std::string &k, const std::string &v, std::string *e) { return m.setValue(target, k, v, e); };
    if (copyMotionSettings(m, src, sink, motionId, err, fromMotion)) return true;
    m = backup;
    return false;
}

// The effective binding of button n in a source: "none" when it has nothing.
struct BorrowedButton {
    bool ok = false;
    std::string action;  // config text ("none", "click", "key:space", "buttonmode:latch", ...)
    std::string label;
    std::string error;
};

inline BorrowedButton borrowedButton(const SettingsModel &m, const BorrowSource &src, int n, const ButtonModeSet *buttonModes) {
    BorrowedButton r;
    if (n < 1 || n > int(kMaxButtons)) {
        r.error = "no such button";
        return r;
    }
    if (src.kind == BorrowSource::Kind::ButtonMode) {
        const ButtonMode *b = buttonModes ? buttonModes->find(src.id) : nullptr;
        if (!b) {
            r.error = "no such button mode";
            return r;
        }
        r.action = b->cycle.empty() || b->target.empty() ? b->action : "mode:" + b->target;
        r.label = "Button mode: " + (b->label.empty() ? b->id : b->label);
    } else if (src.kind == BorrowSource::Kind::JoystickMode) {
        const ModeDef *d = m.modeDef(src.id);
        if (!d) {
            r.error = "no such joystick mode";
            return r;
        }
        r.action = "none";
        for (const ButtonBinding &bb : d->buttons)
            if (bb.button == n && bb.type != ButtonActionType::Unbound) r.action = modelist::buttonActionText(bb);
        r.label = "Joystick mode: " + borrow_detail::modeName(*d);
    } else {
        if (src.section >= m.sectionCount()) {
            r.error = "no such profile";
            return r;
        }
        const size_t i = src.section;
        const ModeDef *d = m.modeDef(m.effectiveMode(i));
        const bool isApp = m.sections()[i].kind == Section::APP;
        const std::string key = "BUTTON_" + std::to_string(n);
        r.action = "none";
        const ButtonBinding *bound = nullptr;
        if (d)
            for (const ButtonBinding &bb : d->buttons)
                if (bb.button == n && bb.type != ButtonActionType::Unbound) bound = &bb;
        if (bound && !(isApp && m.isOverridden(i, key))) {
            r.action = modelist::buttonActionText(*bound);
        } else {
            const Settings st = m.effectiveWithMode(i);
            const auto it = st.buttons.find(n);
            if (it != st.buttons.end() && !it->second.empty()) r.action = it->second;
        }
        r.label = borrow_detail::profileLabel(m, i);
    }
    r.ok = true;
    return r;
}

inline std::string borrowedButtonWords(const SettingsModel &m, const BorrowSource &src, int n, const ButtonModeSet *buttonModes,
                                       const StringProvider &s) {
    const BorrowedButton b = borrowedButton(m, src, n, buttonModes);
    if (!b.ok) return s.tr(b.error.c_str());
    const std::string what = b.action == "none" ? s.tr("nothing") : buttonActionWords(b.action, s);
    return format(s.tr("In %1 this button does: %2. Taking it over copies this binding into the profile; later changes in the source do not follow."),
                  {sourceLabel(b.label, s), what});
}

inline bool borrowButton(SettingsModel &m, size_t target, int n, const BorrowSource &src, std::string *err,
                         const ButtonModeSet *buttonModes = nullptr) {
    auto fail = [&](const std::string &e) {
        if (err) *err = e;
        return false;
    };
    if (target >= m.sectionCount()) return fail("no such profile");
    if (src.kind == BorrowSource::Kind::Profile && src.section == target) return fail("take it over from another profile");
    const BorrowedButton b = borrowedButton(m, src, n, buttonModes);
    if (!b.ok) return fail(b.error);
    return m.setButton(target, n, b.action, err);
}

// ---- into a joystick mode being edited (the Joystick modes page): the same copy, written into the form of the mode ----
// The form is the one of ModeEditor; the action goes to the motion through assign() (motion_map_model.hpp), so a push and a
// tilt that end up the same stay one unsplit channel key. A button is copied as its binding.
inline bool borrowMotionIntoMode(const SettingsModel &m, ModeForm &form, const Settings &base, Motion motion, const BorrowSource &src,
                                 std::string *err, Motion from = Motion::Lift, bool tuning = false) {
    const Motion fromMotion = from == Motion::Lift ? motion : from;
    const BorrowedMotion b = borrowedMotion(m, src, motionId(fromMotion));
    if (!b.ok) {
        if (err) *err = b.error;
        return false;
    }
    ChannelForm f = channelFormOf(b.action);
    if (b.off) f = ChannelForm(), f.type = ChannelActionType::Off;
    const ModeForm backup = form;
    const AssignResult r = assign(form, base, motion, f);
    if (!r.ok) {
        if (err) *err = r.error;
        return false;
    }
    if (!tuning) return true;
    const TuningSink sink = [&](const std::string &k, const std::string &v, std::string *) {
        form.setOverride(k, v);
        return true;
    };
    if (copyMotionSettings(m, src, sink, motionId(motion), err, motionId(fromMotion))) return true;
    form = backup;
    return false;
}

inline bool borrowButtonIntoMode(const SettingsModel &m, ModeForm &form, int n, const BorrowSource &src, const ButtonModeSet *buttonModes,
                                 std::string *err) {
    const BorrowedButton b = borrowedButton(m, src, n, buttonModes);
    if (!b.ok) {
        if (err) *err = b.error;
        return false;
    }
    form.setButton(n, b.action);
    return true;
}

}  // namespace spn::ui
