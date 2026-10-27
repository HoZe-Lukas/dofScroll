// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Editing one motion of the cap in a PROFILE (Global, device or program), toolkit-free. The Motions and buttons
// tab shows the six motions; a motion has three states in a profile:
//
//   Inherited  the profile sets nothing for it: what Global (and the device profile) and the joystick mode say applies
//   SetHere    the profile tunes the channel of the motion itself (SENS, MIX, DEADZONE, SIGN, CURVE or SMOOTH_MS)
//   Cleared    the profile switched the channel off: <CH>_SENS = 0, nothing happens for the motion in this profile
//
// No new config key: a channel is the vertical scroll (SCROLL_V_, vertical push and tilt), the horizontal scroll
// (SCROLL_H_, horizontal push and tilt) or the twist (ZOOM_); the three states are stored in the keys of that
// channel in the section of the profile, "Cleared" is <CH>_SENS = 0 and the daemon stops that channel completely
// (no steps, no zoom key held, a running zoom ends with its keys released: tests/test_mode_runner.cpp).
// What a profile cannot hold is another ACTION for a motion (V_ACTION, TWIST_ACTION ...): that belongs to the
// joystick mode, chosen per profile with MODE (docs/modes.md). Lift / press has no channel of its own.
//
//   const MotionEdit e = motionEdit(model, section, "twist", strings);   // state, words, which buttons make sense
//   clearMotion(model, section, "twist", &memory, &err);                  // ZOOM_SENS = 0
//   switchOnMotion(model, section, "twist", &memory, &err);               // the sensitivity again, never left at 0
//   inheritMotion(model, section, "twist", &memory, &err);                // back to the inherited setting
#pragma once
#include <map>
#include <string>
#include <vector>

#include "config.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

enum class MotionState { Inherited, SetHere, Cleared };

struct MotionEdit {
    std::string id;       // "v_push" ... "twist", "lift"
    std::string prefix;   // "SCROLL_V_", "SCROLL_H_", "ZOOM_"; "" for lift / press
    std::string shared;   // the other motion of the same channel ("Vertical tilt", English source), "" for the twist
    bool editable = false;
    MotionState state = MotionState::Inherited;
    bool on = true;            // the channel does something in this profile (effective sensitivity not 0)
    bool canClear = false;     // not cleared yet
    bool canInherit = false;   // the profile sets something for the channel
    bool canSwitchOn = false;  // the channel is off here (cleared here or inherited off)
    std::string words;         // one sentence: the state and where it comes from
};

// The number a profile had before the channel was cleared, per profile and channel, for the session.
using MotionMemory = std::map<std::string, std::string>;

namespace motion_edit_detail {
inline const char *const kChannelKeys[] = {"MIX", "SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"};

inline std::string prefixOf(const std::string &motionId) {
    if (motionId == "v_push" || motionId == "v_tilt") return "SCROLL_V_";
    if (motionId == "h_push" || motionId == "h_tilt") return "SCROLL_H_";
    if (motionId == "twist") return "ZOOM_";
    return std::string();
}

inline int sensOf(const Settings &s, const std::string &prefix) {
    return prefix == "SCROLL_V_" ? s.scroll_v.sensitivity : prefix == "SCROLL_H_" ? s.scroll_h.sensitivity : s.zoom.sensitivity;
}

// The factory sensitivity of the channel (config.hpp defaults): what "switch on" writes when nothing inherited is on.
inline int factorySens(const std::string &prefix) { return sensOf(Settings(), prefix); }

inline std::string memoryKey(const SettingsModel &m, size_t section, const std::string &prefix) {
    const SettingsModel::SectionInfo info = m.sections()[section];
    return std::to_string(int(info.kind)) + ":" + info.pattern + ":" + prefix;
}

inline std::string ownSens(const SettingsModel &m, size_t section, const std::string &prefix, bool *has) {
    SettingsModel::Value v;
    *has = m.value(section, prefix + "SENS", &v) && v.overridden;
    return *has ? v.text : std::string();
}

inline bool effectiveOn(const SettingsModel &m, size_t section, const std::string &prefix) {
    return sensOf(m.effectiveWithMode(section), prefix) != 0;
}
}  // namespace motion_edit_detail

inline MotionEdit motionEdit(const SettingsModel &m, size_t section, const std::string &motionId, const StringProvider &s) {
    using namespace motion_edit_detail;
    MotionEdit e;
    e.id = motionId;
    e.prefix = prefixOf(motionId);
    if (e.prefix.empty() || section >= m.sectionCount()) return e;
    e.editable = true;
    if (motionId == "v_push") e.shared = "Vertical tilt";
    else if (motionId == "v_tilt") e.shared = "Vertical push";
    else if (motionId == "h_push") e.shared = "Horizontal tilt";
    else if (motionId == "h_tilt") e.shared = "Horizontal push";
    bool hasSens = false;
    const std::string own = ownSens(m, section, e.prefix, &hasSens);
    bool setHere = false;
    for (const char *k : kChannelKeys)
        if (m.isOverridden(section, e.prefix + k)) setHere = true;
    e.on = effectiveOn(m, section, e.prefix);
    const bool clearedHere = hasSens && own == "0";
    e.state = clearedHere ? MotionState::Cleared : setHere ? MotionState::SetHere : MotionState::Inherited;
    e.canClear = !clearedHere;
    e.canInherit = setHere;
    e.canSwitchOn = !e.on;

    const KeyOrigin o = keyOrigin(m, section, e.prefix + "SENS");
    const std::string onOff = e.on ? s.tr("On") : s.tr("Off");
    if (section == 0) {
        if (clearedHere) e.words = s.tr("Cleared in Global: nothing happens for this motion unless a profile sets it again.");
        else if (setHere) e.words = format(s.tr("%1. Tuned in Global."), {onOff});
        else e.words = e.on ? s.tr("On (the built-in default).") : s.tr("Off: the sensitivity is 0.");
        return e;
    }
    const std::string inheritedState = sensOf(m.effective(0), e.prefix) != 0 ? s.tr("on") : s.tr("off");
    switch (e.state) {
        case MotionState::Cleared:
            e.words = format(s.tr("Cleared in this profile: nothing happens for this motion here (Global: %1)."), {inheritedState});
            break;
        case MotionState::SetHere:
            e.words = format(s.tr("%1. Set in this profile (Global: %2)."), {onOff, inheritedState});
            break;
        case MotionState::Inherited: {
            std::string from = originBadgeText(o, s);
            if (from.empty()) from = s.tr("Inherited");
            e.words = format(s.tr("%1. %2."), {onOff, from});
            break;
        }
    }
    return e;
}

// Cleared: <CH>_SENS = 0 in this profile (also when the inherited value is 0 already: it pins the channel off). The
// number the profile had is remembered for "Switch on". false + *err when the joystick mode keeps the channel on
// (a mode override beats [global] and the device profile; only a program profile beats it); nothing is changed then.
inline bool clearMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory, std::string *err) {
    using namespace motion_edit_detail;
    const std::string prefix = prefixOf(motionId);
    if (prefix.empty() || section >= m.sectionCount()) return false;
    bool had = false;
    const std::string before = ownSens(m, section, prefix, &had);
    if (had && before == "0") return true;
    if (!m.setValue(section, prefix + "SENS", "0", err)) return false;
    if (effectiveOn(m, section, prefix)) {
        if (had) m.setValue(section, prefix + "SENS", before, nullptr);
        else m.clearOverride(section, prefix + "SENS");
        if (err) *err = "the joystick mode of this profile sets the sensitivity of this channel; change it there";
        return false;
    }
    if (memory && had) (*memory)[memoryKey(m, section, prefix)] = before;
    return true;
}

// Back to the inherited setting. A cleared motion: only the 0 goes (a tuning of the channel stays and is then
// "set here"); a motion set here: every key of its channel goes.
inline bool inheritMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory) {
    using namespace motion_edit_detail;
    const std::string prefix = prefixOf(motionId);
    if (prefix.empty() || section >= m.sectionCount()) return false;
    bool had = false;
    const std::string own = ownSens(m, section, prefix, &had);
    if (had && own == "0") {
        m.clearOverride(section, prefix + "SENS");
    } else {
        for (const char *k : kChannelKeys) m.clearOverride(section, prefix + k);
    }
    if (memory) memory->erase(memoryKey(m, section, prefix));
    return true;
}

// The channel does something again, never left at sensitivity 0: the number the profile had before it was cleared,
// else the inherited setting (the override goes); when that is off too (switched off in Global or in the mode), the
// factory sensitivity of the channel is written in this profile.
inline bool switchOnMotion(SettingsModel &m, size_t section, const std::string &motionId, MotionMemory *memory, std::string *err) {
    using namespace motion_edit_detail;
    const std::string prefix = prefixOf(motionId);
    if (prefix.empty() || section >= m.sectionCount()) return false;
    const std::string key = prefix + "SENS";
    const std::string mk = memoryKey(m, section, prefix);
    std::string remembered;
    if (memory && memory->count(mk)) remembered = (*memory)[mk];
    m.clearOverride(section, key);
    const int inherited = sensOf(m.effectiveWithMode(section), prefix);
    if (!remembered.empty() && remembered != "0" && std::to_string(inherited) != remembered) {
        if (!m.setValue(section, key, remembered, err)) return false;
    } else if (inherited == 0) {
        if (!m.setValue(section, key, std::to_string(factorySens(prefix)), err)) return false;
    }
    if (!effectiveOn(m, section, prefix)) {
        m.clearOverride(section, key);
        if (err) *err = "the joystick mode of this profile sets the sensitivity of this channel to 0; change it there";
        return false;
    }
    if (memory) memory->erase(mk);
    return true;
}

}  // namespace spn::ui
