// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The tuning of ONE motion (vertical or horizontal push or tilt) in a PROFILE (Global, device or program), toolkit-free.
// Sensitivity, dead zone, direction, curve and smoothing of the push and of the tilt are independent: they are written as the
// per-part keys SCROLL_<V|H>_<PUSH|TILT>_<...> of the profile, never as the keys of the whole channel (SCROLL_V_SENS ...,
// which stay valid and are the starting values of both parts). The keys of a part act only while the channel is split, so
// the first edit of a part of an unsplit channel splits it without changing what any motion does (part_tuning.hpp has the
// rules). The twist (ZOOM_*) and the lift (LIFT_*) have keys of their own and are not handled here.
//
//   const PartTuningView v = partTuningView(model, section, "v_tilt", strings);   // values and where they come from, in words
//   setPartTuning(model, section, "v_tilt", "CURVE", "2", &err);                    // validates, splits if needed, writes
//   clearPartTuning(model, section, "v_tilt", "CURVE");                             // the motion follows the channel again
//   splitChannelFor(model, section, "v_tilt", editedKey, &err);                     // the split alone (key function edits)
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/part_tuning.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct PartTuningField {
    std::string suffix;  // "SENS" ...
    std::string key;     // "SCROLL_V_TILT_SENS"
    std::string label;   // English source text ("Sensitivity")
    std::string value;   // what the motion runs with (canonical config text)
    bool own = false;    // the profile holds the key itself: Inherit / Reset removes it
    std::string words;   // where the value comes from, in words (English source text through the StringProvider)
};

struct PartTuningView {
    bool partMotion = false;  // false: the motion is not a push or tilt (twist, lift): use the channel keys of the motion
    bool split = false;       // the channel is split now: the keys of the part act
    std::string note;         // one sentence about the split state
    std::vector<PartTuningField> fields;
};

namespace part_tuning_detail {
inline const ModeDef &modeOf(const SettingsModel &m, size_t section) {
    static const ModeDef normal;
    const ModeDef *def = m.modeDef(m.effectiveMode(section));
    return def ? *def : normal;
}
inline ChannelActions actionsOf(const SettingsModel &m, size_t section) {
    return effectiveChannelActions(modeOf(m, section), m.effectiveWithMode(section));
}
inline bool splitNow(const SettingsModel &m, size_t section, int part) {
    const ChannelActions ca = actionsOf(m, section);
    return part < 2 ? ca.vSplit.active() : ca.hSplit.active();
}
inline std::string keyOf(int part, const char *suffix) { return part_tuning::partPrefix(part) + suffix; }

// The split itself on `work` (a copy the caller throws away on failure); false + *err when the joystick mode of the profile keeps
// the channel unsplit (a layer above the profile sets the channel action, only a program profile has the last word).
inline bool splitInto(SettingsModel &work, size_t section, int part, const std::string &editedKey, std::string *err) {
    const int channel = part / 2;
    const Settings before = work.effectiveWithMode(section);
    const ChannelActions ca = actionsOf(work, section);
    const ChannelAction parent = channel == 0 ? ca.v : ca.h;
    for (const KV &kv : part_tuning::splitPins(before, channel, editedKey))
        if (!work.setValue(section, kv.first, kv.second, err)) return false;
    const std::string text = modelist::channelActionText(parent);
    const int push = channel * 2;
    for (int p = push; p <= push + 1; ++p)
        if (!work.setValue(section, p == push ? (channel == 0 ? "V_PUSH_ACTION" : "H_PUSH_ACTION")
                                              : (channel == 0 ? "V_TILT_ACTION" : "H_TILT_ACTION"),
                           text, err))
            return false;
    // the split must be in force and no motion may do something else: a layer above the profile (the joystick mode) can drop it
    const ChannelActions after = actionsOf(work, section);
    const SplitChannel &sp = channel == 0 ? after.vSplit : after.hSplit;
    if (!sp.active() || !(sp.pushAction(channel == 0 ? after.v : after.h) == parent) ||
        !(sp.tiltAction(channel == 0 ? after.v : after.h) == parent)) {
        if (err)
            *err = "the joystick mode of this profile sets this channel; only a program profile can give push and tilt their own values, or change it in the joystick mode";
        return false;
    }
    return true;
}
}  // namespace part_tuning_detail

// Splits the channel of the motion when it is not split yet (nothing changes for any motion, see part_tuning.hpp); `editedKey` is the
// key the caller writes itself right after. No-op for a split channel and for the twist and the lift. Nothing is written on failure.
inline bool splitChannelFor(SettingsModel &m, size_t section, const std::string &motionId, const std::string &editedKey, std::string *err) {
    using namespace part_tuning_detail;
    const int part = part_tuning::partIndex(motionId);
    if (part < 0 || section >= m.sectionCount()) return true;
    if (splitNow(m, section, part)) return true;
    SettingsModel work = m;
    if (!splitInto(work, section, part, editedKey, err)) return false;
    m = std::move(work);
    return true;
}

// The settings the key function box of a motion shows: an unsplit channel runs the tilt on the push slot, so the tilt shows what the
// push slot holds (what will be copied to the tilt when the channel is split by the first edit).
inline Settings keyEngineSettings(const SettingsModel &m, size_t section, const std::string &motionId) {
    Settings s = m.effectiveWithMode(section);
    const int part = part_tuning::partIndex(motionId);
    if (part == PART_V_TILT || part == PART_H_TILT)
        if (!part_tuning_detail::splitNow(m, section, part)) s.key_tuning[part] = s.key_tuning[part - 1];
    return s;
}

// The values and the origin of each field of the motion.
inline PartTuningView partTuningView(const SettingsModel &m, size_t section, const std::string &motionId, const StringProvider &s) {
    using namespace part_tuning_detail;
    PartTuningView v;
    const int part = part_tuning::partIndex(motionId);
    if (part < 0 || section >= m.sectionCount()) return v;
    v.partMotion = true;
    v.split = splitNow(m, section, part);
    v.note = v.split ? s.tr("Push and tilt of this direction have their own tuning. A value you do not set follows the whole channel.")
                     : s.tr("Push and tilt of this direction share one tuning for now. Changing a value here gives this motion its own; the other motion keeps its tuning.");
    const Settings eff = m.effectiveWithMode(section);
    const std::string chan = part_tuning::channelPrefix(part);
    for (int i = 0; i < part_tuning::kFieldCount; ++i) {
        const part_tuning::Field &f = part_tuning::fields()[i];
        PartTuningField pf;
        pf.suffix = f.suffix;
        pf.label = f.label;
        pf.key = keyOf(part, f.suffix);
        pf.value = part_tuning::fieldText(part_tuning::motionCfg(eff, part, v.split), f.suffix);
        pf.own = motion_edit_detail::ownText(m, section, pf.key, nullptr);
        const std::string shown = displayValue(pf.key, pf.value, s);
        if (pf.own) {
            SettingsModel tmp = m;
            tmp.clearOverride(section, pf.key);
            const std::string without = displayValue(
                pf.key, part_tuning::fieldText(part_tuning::motionCfg(tmp.effectiveWithMode(section), part, splitNow(tmp, section, part)), f.suffix), s);
            pf.words = section == 0 ? format(s.tr("Set in Global for this motion only (otherwise: %1)"), {without})
                                    : format(s.tr("Set in this profile for this motion only (inherited: %1)"), {without});
        } else {
            SettingsModel::Source src;
            if (m.sourceOf(section, pf.key, &src) && src == SettingsModel::Source::Mode) {
                pf.words = format(s.tr("From joystick mode \"%1\": %2"), {m.effectiveMode(section), shown});
            } else if (section > 0 && m.isOverridden(0, pf.key)) {
                pf.words = format(s.tr("Inherited from Global (this motion only): %1"), {shown});
            } else {
                const KeyOrigin o = keyOrigin(m, section, chan + f.suffix);
                const std::string channelWords = originBadgeText(o, s);
                pf.words = section == 0 ? format(s.tr("The same as the whole channel: %1"), {shown})
                                        : format(s.tr("Inherited from the whole channel: %1"), {shown});
                if (!channelWords.empty()) pf.words += " (" + channelWords + ")";
            }
        }
        v.fields.push_back(pf);
    }
    return v;
}

// Writes one field of the motion (validated by the parser of the config; an invalid value is never written). Splits the channel first
// when it is not split. false + *err (English source text) when the value is invalid or the joystick mode keeps the channel unsplit.
inline bool setPartTuning(SettingsModel &m, size_t section, const std::string &motionId, const std::string &suffix,
                          const std::string &text, std::string *err) {
    using namespace part_tuning_detail;
    const int part = part_tuning::partIndex(motionId);
    bool known = false;
    for (int i = 0; i < part_tuning::kFieldCount; ++i) known = known || suffix == part_tuning::fields()[i].suffix;
    if (part < 0 || !known || section >= m.sectionCount()) {
        if (err) *err = "unknown setting";
        return false;
    }
    const std::string key = keyOf(part, suffix.c_str());
    Settings probe;
    if (!applyKey(probe, key, text, err)) {
        if (err && err->empty()) *err = "invalid value";
        return false;
    }
    SettingsModel work = m;
    if (!splitNow(work, section, part) && !splitInto(work, section, part, key, err)) return false;
    if (!work.setValue(section, key, text, err)) return false;
    m = std::move(work);
    return true;
}

namespace part_tuning_detail {
// What a motion really runs with: the five tuning values and the three key function values of the slot it uses.
inline std::vector<std::string> runtimeValues(const SettingsModel &m, size_t section, int part) {
    const Settings s = m.effectiveWithMode(section);
    const bool split = splitNow(m, section, part);
    std::vector<std::string> out;
    const ChannelCfg cfg = part_tuning::motionCfg(s, part, split);
    for (int i = 0; i < part_tuning::kFieldCount; ++i) out.push_back(part_tuning::fieldText(cfg, part_tuning::fields()[i].suffix));
    const KeyTuning &kt = s.key_tuning[split ? part : (part & ~1)];  // an unsplit channel runs its tilt on the push slot
    for (const char *k : {"KEY_RATE_MIN", "KEY_RATE_MAX", "TRIGGER"}) out.push_back(part_tuning::keyFieldText(kt, k));
    out.push_back(std::to_string(kt.set));
    return out;
}
}  // namespace part_tuning_detail

// The motion follows the whole channel again for this field (the key goes). The channel stays split, unless the split was only there
// for this tuning: when the profile holds no other value of the channel's push or tilt, the two part actions the first edit wrote
// go again (so a profile that tunes nothing any more is plain again), provided no motion would do anything else without them.
inline bool clearPartTuning(SettingsModel &m, size_t section, const std::string &motionId, const std::string &suffix) {
    using namespace part_tuning_detail;
    const int part = part_tuning::partIndex(motionId);
    if (part < 0 || section >= m.sectionCount()) return false;
    m.clearOverride(section, keyOf(part, suffix.c_str()));
    const int channel = part / 2;
    const std::string pushPre = part_tuning::partPrefix(channel * 2), tiltPre = part_tuning::partPrefix(channel * 2 + 1);
    for (const KV &kv : m.profiles().sections[section].kv) {
        const std::string k = utf8::upperAscii(kv.first);
        if (k.compare(0, pushPre.size(), pushPre) == 0 || k.compare(0, tiltPre.size(), tiltPre) == 0) return true;  // still tuned
    }
    const char *pushKey = channel == 0 ? "V_PUSH_ACTION" : "H_PUSH_ACTION", *tiltKey = channel == 0 ? "V_TILT_ACTION" : "H_TILT_ACTION";
    if (!motion_edit_detail::ownText(m, section, pushKey, nullptr) || !motion_edit_detail::ownText(m, section, tiltKey, nullptr)) return true;
    SettingsModel work = m;
    work.clearOverride(section, pushKey);
    work.clearOverride(section, tiltKey);
    for (int p = channel * 2; p <= channel * 2 + 1; ++p)
        if (runtimeValues(work, section, p) != runtimeValues(m, section, p) ||
            !(motion_edit_detail::effectiveAction(work, section, part_tuning::partId(p)) == motion_edit_detail::effectiveAction(m, section, part_tuning::partId(p))))
            return true;
    m = std::move(work);
    return true;
}

}  // namespace spn::ui
