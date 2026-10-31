// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The tuning of ONE motion of a scroll channel (vertical or horizontal push, vertical or horizontal tilt), toolkit-free
// and independent of where it is stored (a profile section or a joystick mode form).
//
// The engine has two levels of tuning for a scroll channel:
//   channel  SCROLL_<V|H>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>   one value for push AND tilt together
//   part     SCROLL_<V|H>_<PUSH|TILT>_<same suffixes>             absolute values of one part, but ONLY while the channel is
//            split (a part action V_PUSH_ACTION ... is set in some layer); an unsplit channel runs push and tilt as one mix
// A control that is labelled for one motion must change that motion only. So the first time a value of a part is edited while
// its channel is not split, the channel is split first, WITHOUT changing what any motion does:
//   - both parts get the action the channel has now (a part action "default" would mean "scroll as usual" and would lose a
//     channel action such as keys:, so the action text of the channel is written, "default" only when that is the action),
//   - values that part keys hold without acting yet (they are ignored while the channel is not split) are pinned to what the
//     motion really does, so the split does not switch them on,
//   - the key functions (SCROLL_<V|H>_TILT_KEY_RATE_MIN ...) of an unsplit channel run on the push slot: the tilt gets the same
//     values, so it presses at the same rates as before.
// Everything a part does not set follows the whole channel (partConfig), so the other motion keeps its inherited tuning.
// What does change by splitting: a push and a tilt deflected at the same moment are two motions now, each with its own dead
// zone, curve and wheel remainder, instead of one sum through one curve (identical while the curve is linear and the
// dead zone is 0).
//
//   part_tuning::partIndex("v_tilt")                      // 1 (SplitPart), -1 for twist, lift and unknown ids
//   part_tuning::splitPins(effective, channel, editedKey) // key / value pairs to write together with the split
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"

namespace spn::ui::part_tuning {

struct Field {
    const char *suffix;  // SENS ...
    const char *label;   // English source text of the UI label
    unsigned bit;        // PartTuning::Field
};
inline const Field *fields() {
    static const Field f[5] = {{"SENS", "Sensitivity", PartTuning::F_SENS},
                               {"DEADZONE", "Dead zone", PartTuning::F_DEADZONE},
                               {"SIGN", "Direction (sign)", PartTuning::F_SIGN},
                               {"CURVE", "Curve", PartTuning::F_CURVE},
                               {"SMOOTH_MS", "Smoothing (ms)", PartTuning::F_SMOOTH}};
    return f;
}
constexpr int kFieldCount = 5;

// "v_push" -> PART_V_PUSH ... ; -1 for anything else.
inline int partIndex(const std::string &motionId) {
    if (motionId == "v_push") return PART_V_PUSH;
    if (motionId == "v_tilt") return PART_V_TILT;
    if (motionId == "h_push") return PART_H_PUSH;
    if (motionId == "h_tilt") return PART_H_TILT;
    return -1;
}
inline const char *partId(int part) {
    static const char *const ids[PART_COUNT] = {"v_push", "v_tilt", "h_push", "h_tilt"};
    return part >= 0 && part < PART_COUNT ? ids[part] : "";
}
// "SCROLL_V_PUSH_" ... and the channel's "SCROLL_V_".
inline std::string partPrefix(int part) { return keyTuningPrefix(part); }
inline std::string channelPrefix(int part) { return part < 2 ? "SCROLL_V_" : "SCROLL_H_"; }
// The motion is the push or tilt of a scroll channel.
inline bool isPartMotion(const std::string &motionId) { return partIndex(motionId) >= 0; }

// The canonical config text of one field of a channel config ("-1", "1.5", "0:0 50:1", "12").
inline std::string fieldText(const ChannelCfg &c, const char *suffix) {
    Settings tmp;
    tmp.parts[0].set = PartTuning::F_SENS | PartTuning::F_DEADZONE | PartTuning::F_SIGN | PartTuning::F_CURVE | PartTuning::F_SMOOTH;
    tmp.parts[0].cfg = c;
    const std::string key = std::string("SCROLL_V_PUSH_") + suffix;
    for (const KV &kv : allKeys(tmp))
        if (kv.first == key) return kv.second;
    return std::string();
}

// What a motion really runs with: the parent channel, plus the part's own values while the channel is split.
inline ChannelCfg motionCfg(const Settings &s, int part, bool split) {
    const ChannelCfg &parent = part < 2 ? s.scroll_v : s.scroll_h;
    return split ? partConfig(parent, s.parts[part], Mix()) : parent;
}

inline std::string keyFieldText(const KeyTuning &t, const char *suffix) {
    Settings tmp;
    tmp.key_tuning[0] = t;
    tmp.key_tuning[0].set = KeyTuning::F_RATE_MIN | KeyTuning::F_RATE_MAX | KeyTuning::F_TRIGGER;
    const std::string key = std::string(keyTuningPrefix(0)) + suffix;
    for (const KV &kv : allKeys(tmp))
        if (kv.first == key) return kv.second;
    return std::string();
}

// What has to be written together with the split of a scroll channel (0 = vertical, 1 = horizontal) so that no motion changes
// (see the top). `s` is the effective configuration BEFORE the split, `editedKey` is the key the caller writes itself right after
// (it is left out). Part tuning keys that do not act yet are pinned to the value the motion really runs with; the tilt's key
// function values follow the push slot the unsplit channel used.
inline std::vector<KV> splitPins(const Settings &s, int channel, const std::string &editedKey) {
    std::vector<KV> out;
    auto add = [&](const std::string &key, const std::string &value) {
        if (key != editedKey) out.emplace_back(key, value);
    };
    const ChannelCfg &parent = channel == 0 ? s.scroll_v : s.scroll_h;
    for (int p = channel * 2; p <= channel * 2 + 1; ++p)
        for (int i = 0; i < kFieldCount; ++i) {
            const Field &f = fields()[i];
            if (!(s.parts[p].set & f.bit)) continue;
            const std::string parentText = fieldText(parent, f.suffix);
            if (fieldText(partConfig(parent, s.parts[p], Mix()), f.suffix) != parentText) add(partPrefix(p) + f.suffix, parentText);
        }
    const int push = channel * 2, tilt = push + 1;
    static const struct { const char *suffix; unsigned bit; } kf[3] = {
        {"KEY_RATE_MIN", KeyTuning::F_RATE_MIN}, {"KEY_RATE_MAX", KeyTuning::F_RATE_MAX}, {"TRIGGER", KeyTuning::F_TRIGGER}};
    KeyTuning defaults;  // what an unset tilt value means: no lower limit, no upper limit below the hard cap, STEP_TRIGGER
    defaults.rate_min = 0.0;
    defaults.rate_max = kMaxKeyRate;
    defaults.trigger = s.step_trigger;
    for (const auto &k : kf) {
        const std::string key = std::string(keyTuningPrefix(tilt)) + k.suffix;
        if (s.key_tuning[push].set & k.bit) add(key, keyFieldText(s.key_tuning[push], k.suffix));
        else if (s.key_tuning[tilt].set & k.bit) add(key, keyFieldText(defaults, k.suffix));
    }
    return out;
}

}  // namespace spn::ui::part_tuning
