// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Editing one motion of the cap in a PROFILE (Global, device or program), toolkit-free. The Motions and buttons
// tab shows the six motions; every one can be edited on its own, and has three states in a profile:
//
//   Inherited  the profile sets nothing for it: what Global (and the device profile), the joystick mode say applies
//   SetHere    the profile gives the motion another action, or tunes the channel (SENS, MIX, DEADZONE, ...)
//   Cleared    the profile switched the motion off: its action is "none" here, nothing happens for it in this profile
//
// Stored with the keys of the joystick mode files, allowed in every profile section (config.hpp ProfileActions,
// profile_actions.hpp for the precedence global < device < joystick mode < program):
//   vertical push V_PUSH_ACTION   vertical tilt V_TILT_ACTION   horizontal push H_PUSH_ACTION
//   horizontal tilt H_TILT_ACTION   twist TWIST_ACTION   (V_ACTION / H_ACTION set both parts of a channel)
// Only what the profile changes is written; "Use the inherited setting" removes the key. A legacy `<CH>_SENS = 0`
// (channel off) is read as "cleared" too and removed again by "Use the inherited setting". Lift / press has its own
// key LIFT_ACTION (and LIFT_SENS ...); its default action is none, it takes the analog-free choices only
// (liftActionChoices: no analog keys) and says in words that lifting fires when the cap is grabbed.
//
//   const MotionEdit e = motionEdit(model, section, "twist", strings);        // state, words, which buttons make sense
//   setMotionAction(model, section, "twist", form, &err);                       // another action, validated
//   clearMotion(model, section, "v_tilt", &memory, &err);                       // V_TILT_ACTION = none
//   switchOnMotion(model, section, "twist", &memory, &err);                     // does something again, never "none"
//   inheritMotion(model, section, "twist", &memory);                            // back to the inherited setting
//   motionChoiceCheck(model, section, "v_tilt", ChannelActionType::Zoom, strings);  // works? and what to know, in words
//   (taking a motion over from another profile or joystick mode: borrow_model.hpp)
#pragma once
#include <map>
#include <string>
#include <vector>

#include "config.hpp"
#include "profile_actions.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

enum class MotionState { Inherited, SetHere, Cleared };

struct MotionEdit {
    std::string id;       // "v_push" ... "twist", "lift"
    std::string prefix;   // channel keys "SCROLL_V_", "SCROLL_H_", "ZOOM_", "LIFT_"
    std::string key;      // the action key of the motion ("V_PUSH_ACTION", "LIFT_ACTION")
    bool editable = false;
    MotionState state = MotionState::Inherited;
    bool on = true;            // the motion does something in this profile
    bool canClear = false;     // not cleared yet
    bool canInherit = false;   // the profile sets something for the motion
    bool canSwitchOn = false;  // the motion does nothing here (cleared here or inherited off)
    ChannelForm action;        // what the motion does now (the form the editor shows)
    std::string words;         // one sentence: the state and where it comes from
};

// The values a profile had before a motion was cleared, per profile and motion, for the session.
using MotionMemory = std::map<std::string, std::string>;

namespace motion_edit_detail {
inline const char *const kChannelKeys[] = {"MIX", "SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"};

inline std::string prefixOf(const std::string &motionId) {
    if (motionId == "v_push" || motionId == "v_tilt") return "SCROLL_V_";
    if (motionId == "h_push" || motionId == "h_tilt") return "SCROLL_H_";
    if (motionId == "twist") return "ZOOM_";
    if (motionId == "lift") return "LIFT_";
    return std::string();
}
// The action key of the motion itself, and the key of its whole channel.
inline std::string partKeyOf(const std::string &id) {
    if (id == "v_push") return "V_PUSH_ACTION";
    if (id == "v_tilt") return "V_TILT_ACTION";
    if (id == "h_push") return "H_PUSH_ACTION";
    if (id == "h_tilt") return "H_TILT_ACTION";
    if (id == "twist") return "TWIST_ACTION";
    if (id == "lift") return "LIFT_ACTION";
    return std::string();
}
inline std::string channelKeyOf(const std::string &id) {
    if (id == "v_push" || id == "v_tilt") return "V_ACTION";
    if (id == "h_push" || id == "h_tilt") return "H_ACTION";
    if (id == "twist") return "TWIST_ACTION";
    if (id == "lift") return "LIFT_ACTION";
    return std::string();
}

inline int sensOf(const Settings &s, const std::string &prefix) {
    return prefix == "SCROLL_V_" ? s.scroll_v.sensitivity
           : prefix == "SCROLL_H_" ? s.scroll_h.sensitivity
           : prefix == "LIFT_"     ? s.lift.sensitivity
                                   : s.zoom.sensitivity;
}

// The factory sensitivity of the channel (config.hpp defaults).
inline int factorySens(const std::string &prefix) { return sensOf(Settings(), prefix); }

inline std::string memoryKey(const SettingsModel &m, size_t section, const std::string &id, const char *what) {
    const SettingsModel::SectionInfo info = m.sections()[section];
    return std::to_string(int(info.kind)) + ":" + info.pattern + ":" + id + ":" + what;
}

// The text the profile itself holds for a key ("" when it does not set it).
inline bool ownText(const SettingsModel &m, size_t section, const std::string &key, std::string *text) {
    for (const KV &kv : m.profiles().sections[section].kv)
        if (utf8::upperAscii(kv.first) == key) {
            if (text) *text = kv.second;
            return true;
        }
    return false;
}

// The action the motion has in the profile now (all layers and the joystick mode merged).
inline ChannelAction effectiveAction(const SettingsModel &m, size_t section, const std::string &id) {
    const Settings s = m.effectiveWithMode(section);
    ModeDef normal;
    const ModeDef *def = m.modeDef(m.effectiveMode(section));
    if (!def) def = &normal;
    const ChannelActions ca = effectiveChannelActions(*def, s);
    if (id == "twist") return ca.twist;
    if (id == "lift") return ca.lift;
    const bool v = id[0] == 'v';
    const SplitChannel &sp = v ? ca.vSplit : ca.hSplit;
    const ChannelAction &parent = v ? ca.v : ca.h;
    return id.find("tilt") != std::string::npos ? sp.tiltAction(parent) : sp.pushAction(parent);
}

inline bool effectiveOn(const SettingsModel &m, size_t section, const std::string &id) {
    const ChannelActionType t = effectiveAction(m, section, id).type;
    if (t == ChannelActionType::Off || (id == "lift" && t == ChannelActionType::Default)) return false;  // the lift's default is none
    return sensOf(m.effectiveWithMode(section), prefixOf(id)) != 0;
}

// The same without what the profile sets for this motion itself: what it would inherit.
inline bool inheritedOn(const SettingsModel &m, size_t section, const std::string &id) {
    SettingsModel tmp = m;
    tmp.clearOverride(section, partKeyOf(id));
    tmp.clearOverride(section, channelKeyOf(id));
    tmp.clearOverride(section, prefixOf(id) + "SENS");
    return effectiveOn(tmp, section, id);
}

// Writes `text` to `key` of the profile and checks that the motion really does `want` afterwards (a profile below the
// joystick mode cannot beat the mode; only a program profile can). On a mismatch the old value is restored and *err
// says why.
inline bool writeAction(SettingsModel &m, size_t section, const std::string &id, const std::string &key,
                        const std::string &text, std::string *err) {
    std::string before;
    const bool had = ownText(m, section, key, &before);
    if (!m.setValue(section, key, text, err)) return false;  // invalid: nothing written
    ChannelAction want;
    parseChannelAction(text, &want, nullptr);
    if (!(effectiveAction(m, section, id) == want)) {
        if (had) m.setValue(section, key, before, nullptr);
        else m.clearOverride(section, key);
        if (err) *err = "the joystick mode of this profile sets this motion; only a program profile can change it, or change it in the joystick mode";
        return false;
    }
    return true;
}
}  // namespace motion_edit_detail

inline MotionEdit motionEdit(const SettingsModel &m, size_t section, const std::string &motionId, const StringProvider &s) {
    using namespace motion_edit_detail;
    MotionEdit e;
    e.id = motionId;
    e.prefix = prefixOf(motionId);
    if (e.prefix.empty() || section >= m.sectionCount()) return e;
    e.editable = true;
    e.key = partKeyOf(motionId);
    std::string own, ownSens;
    const bool hasPart = ownText(m, section, e.key, &own);
    std::string chanText;
    const bool hasChan = !hasPart && ownText(m, section, channelKeyOf(motionId), &chanText);
    if (hasChan) own = chanText;
    const bool hasSens = ownText(m, section, e.prefix + "SENS", &ownSens);
    bool tuned = false;
    for (const char *k : kChannelKeys)
        if (m.isOverridden(section, e.prefix + k)) tuned = true;
    e.on = effectiveOn(m, section, motionId);
    e.action = channelFormOf(effectiveAction(m, section, motionId));
    const bool clearedHere = (hasPart || hasChan) ? utf8::lowerAscii(own) == "none" : (hasSens && ownSens == "0");
    e.state = clearedHere ? MotionState::Cleared : (hasPart || hasChan || tuned) ? MotionState::SetHere : MotionState::Inherited;
    e.canClear = !clearedHere;
    e.canInherit = hasPart || hasChan || tuned;
    e.canSwitchOn = !e.on && motionId != "lift";  // the lift is off by default: it is switched on by choosing an action

    const bool inh = inheritedOn(m, section, motionId);
    const std::string inhWord = inh ? s.tr("on") : s.tr("off");
    const std::string onOff = e.on ? s.tr("On") : s.tr("Off");
    if (section == 0) {
        if (clearedHere) e.words = s.tr("Cleared in Global: nothing happens for this motion unless a profile sets it again.");
        else if (hasPart || hasChan || tuned) e.words = format(s.tr("%1. Set in Global."), {onOff});
        else if (motionId == "lift") e.words = e.on ? s.tr("On.") : s.tr("Off (the built-in default): lifting the cap does nothing.");
        else e.words = e.on ? s.tr("On (the built-in default).") : s.tr("Off: the sensitivity is 0.");
        return e;
    }
    switch (e.state) {
        case MotionState::Cleared:
            e.words = format(s.tr("Cleared in this profile: nothing happens for this motion here (inherited: %1)."), {inhWord});
            break;
        case MotionState::SetHere:
            e.words = format(s.tr("%1. Set in this profile (inherited: %2)."), {onOff, inhWord});
            break;
        case MotionState::Inherited: {
            const KeyOrigin o = keyOrigin(m, section, e.prefix + "SENS");
            std::string from = originBadgeText(o, s);
            if (from.empty()) from = s.tr("Inherited");
            e.words = format(s.tr("%1. %2."), {onOff, from});
            break;
        }
    }
    return e;
}

// Another action for the motion in this profile (validated by the same parser as the joystick mode files; an invalid
// action is never written). false + *err (English source text) when it is invalid or the joystick mode keeps the motion.
inline bool setMotionAction(SettingsModel &m, size_t section, const std::string &motionId, const ChannelForm &form, std::string *err) {
    using namespace motion_edit_detail;
    const std::string key = partKeyOf(motionId);
    if (key.empty() || section >= m.sectionCount()) {
        if (err) *err = "unknown motion";
        return false;
    }
    return writeAction(m, section, motionId, key, form.text(), err);
}

// Cleared: the action of the motion is "none" in this profile only (also when the inherited action is "none" already:
// it pins the motion off). The action the profile had is remembered for "Switch on".
inline bool clearMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory, std::string *err) {
    using namespace motion_edit_detail;
    const std::string key = partKeyOf(motionId);
    if (key.empty() || section >= m.sectionCount()) return false;
    std::string before;
    const bool had = ownText(m, section, key, &before);
    if (had && utf8::lowerAscii(before) == "none") return true;
    if (!writeAction(m, section, motionId, key, "none", err)) return false;
    if (memory && had) (*memory)[memoryKey(m, section, motionId, "action")] = before;
    return true;
}

// Back to the inherited setting: the profile's own action key of the motion goes (the key of its channel when the
// motion has none of its own), and a legacy `<CH>_SENS = 0`; a motion that is only tuned loses the tuning of its channel.
inline bool inheritMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory) {
    using namespace motion_edit_detail;
    const std::string key = partKeyOf(motionId), prefix = prefixOf(motionId);
    if (key.empty() || section >= m.sectionCount()) return false;
    bool removed = false;
    if (m.isOverridden(section, key)) {
        m.clearOverride(section, key);
        removed = true;
    } else if (m.isOverridden(section, channelKeyOf(motionId))) {
        m.clearOverride(section, channelKeyOf(motionId));
        removed = true;
    }
    std::string sens;
    if (ownText(m, section, prefix + "SENS", &sens) && sens == "0") {
        m.clearOverride(section, prefix + "SENS");
        removed = true;
    }
    if (!removed)
        for (const char *k : kChannelKeys) m.clearOverride(section, prefix + k);
    if (memory) {
        memory->erase(memoryKey(m, section, motionId, "action"));
        memory->erase(memoryKey(m, section, motionId, "sens"));
    }
    return true;
}

// The motion does something again, never left "none" or at sensitivity 0: the action the profile had before it was
// cleared, else the inherited action (the key goes); when that is "none" too (switched off in Global or by the joystick
// mode) the normal action of the motion ("default": scroll or zoom) is written in this profile.
inline bool switchOnMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory, std::string *err) {
    using namespace motion_edit_detail;
    const std::string key = partKeyOf(motionId), prefix = prefixOf(motionId);
    if (key.empty() || section >= m.sectionCount()) return false;
    std::string own;
    const bool hadPart = ownText(m, section, key, &own);
    const bool hadChan = !hadPart && ownText(m, section, channelKeyOf(motionId), &own);
    if ((hadPart || hadChan) && utf8::lowerAscii(own) == "none") {
        std::string remembered;
        if (memory && memory->count(memoryKey(m, section, motionId, "action")))
            remembered = (*memory)[memoryKey(m, section, motionId, "action")];
        m.clearOverride(section, hadPart ? key : channelKeyOf(motionId));
        if (!remembered.empty() && utf8::lowerAscii(remembered) != "none")
            if (!writeAction(m, section, motionId, key, remembered, nullptr)) m.clearOverride(section, key);
    }
    // a legacy sensitivity 0 (written by hand or by an older version of this editor), or an inherited one
    std::string sens;
    if (ownText(m, section, prefix + "SENS", &sens) && sens == "0") m.clearOverride(section, prefix + "SENS");
    if (sensOf(m.effectiveWithMode(section), prefix) == 0 &&
        !m.setValue(section, prefix + "SENS", std::to_string(factorySens(prefix)), err))
        return false;
    if (effectiveAction(m, section, motionId).type == ChannelActionType::Off && !writeAction(m, section, motionId, key, "default", err))
        return false;
    if (!effectiveOn(m, section, motionId)) {
        if (err) *err = "the joystick mode of this profile keeps this motion off; change it there";
        return false;
    }
    if (memory) {
        memory->erase(memoryKey(m, section, motionId, "action"));
        memory->erase(memoryKey(m, section, motionId, "sens"));
    }
    return true;
}

// Can the profile give the motion this action, and what is worth knowing about it, in words. The choices are the same for every
// motion (any motion can get any action, including zoom on a tilt); what does not work is said here and the view disables the
// choice: the check runs the real write on a copy of the model, so it can never disagree with what setMotionAction does.
struct MotionChoiceCheck {
    bool works = true;
    std::string note;  // English source text through the StringProvider; empty when there is nothing to say
};
inline MotionChoiceCheck motionChoiceCheck(const SettingsModel &m, size_t section, const std::string &motionId, ChannelActionType type,
                                           const StringProvider &s) {
    using namespace motion_edit_detail;
    MotionChoiceCheck c;
    const std::string key = partKeyOf(motionId);
    if (key.empty() || section >= m.sectionCount()) {
        c.works = false;
        return c;
    }
    if (motionId == "lift" && type == ChannelActionType::Keys) {  // the parser would refuse it as well (parseLiftAction)
        c.works = false;
        c.note = s.tr("An analog key repeat is not offered for lift / press: it fires when you grab the cap. Use one key tap per push.");
        return c;
    }
    SettingsModel tmp = m;
    ChannelForm f;
    f.type = type;
    std::string err;
    if (!setMotionAction(tmp, section, motionId, f, &err)) {
        c.works = false;
        c.note = s.tr("The joystick mode of this profile sets this motion; only a program profile can change it, or change it in the joystick mode.");
        return c;
    }
    if (motionId == "lift" && type != ChannelActionType::Default && type != ChannelActionType::Off)
        c.note = s.tr("Lift / press can fire when you grab or let go of the cap. Raise the dead zone (LIFT_DEADZONE) if it does.") + " ";
    if (type == ChannelActionType::Zoom && motionId != "twist") {
        c.note += s.tr("Zoom from this motion holds the zoom keys and sends the wheel while the motion is moved. It uses the sensitivity, dead zone and direction (sign) of its own channel, not those of the twist; flip the direction there if it zooms the wrong way. While it zooms, the other wheel motions of this profile pause. The twist keeps its own zoom.");
        if (m.effectiveWithMode(section).zoom_modifier == 0)
            c.note += " " + s.tr("The zoom keys are set to none for this profile, so the plain wheel is sent and it scrolls instead of zooming.");
    } else if (type == ChannelActionType::Zoom && m.effectiveWithMode(section).zoom_modifier == 0) {
        c.note += s.tr("The zoom keys are set to none for this profile, so the plain wheel is sent and it scrolls instead of zooming.");
    }
    return c;
}

// The choices of the "What it does" list of a motion, in display order (English source labels; the view translates).
struct MotionActionChoice {
    ChannelActionType type;
    const char *label;
};
inline const std::vector<MotionActionChoice> &motionActionChoices() {
    static const std::vector<MotionActionChoice> c = {{ChannelActionType::Default, "Normal (scroll, or zoom for the twist)"},
                                                      {ChannelActionType::WheelV, "Scroll"},
                                                      {ChannelActionType::WheelH, "Scroll sideways"},
                                                      {ChannelActionType::Zoom, "Zoom"},
                                                      {ChannelActionType::Keys, "Keys (one key per direction)"},
                                                      {ChannelActionType::Step, "One key tap per push"},
                                                      {ChannelActionType::Off, "Nothing"}};
    return c;
}
// Lift / press: nothing is its default, then the functions that make sense on a motion that can fire when the cap is
// grabbed: scroll, zoom and one key tap per push. No analog keys (parseLiftAction refuses them).
inline const std::vector<MotionActionChoice> &liftActionChoices() {
    static const std::vector<MotionActionChoice> c = {{ChannelActionType::Default, "Nothing (the default)"},
                                                      {ChannelActionType::WheelV, "Scroll"},
                                                      {ChannelActionType::WheelH, "Scroll sideways"},
                                                      {ChannelActionType::Zoom, "Zoom"},
                                                      {ChannelActionType::Step, "One key tap per push"}};
    return c;
}

}  // namespace spn::ui
