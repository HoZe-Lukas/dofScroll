// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The analog / digital choice of a key function and its rate and trigger fields (stage 3 of #224), toolkit-free, shared by the
// profile motions page and the joystick mode editor. A key function (`keys:` / `step:`) of a motion is analog (the key repeats
// faster the further you push) or digital (one press per push, like a click). The check box only switches the prefix of the
// stored action text; the fields are the keys <P>KEY_RATE_MIN, <P>KEY_RATE_MAX and <P>TRIGGER of the motion (config.hpp,
// KeyTuning), validated by the same applyKey as everywhere. Where analog is not allowed (a key of tier B, the lift) the box
// is greyed and says why.
//
//   const AnalogChoice c = analogChoice("v_push", form, strings);      // shown? checked? greyed, and the reason in words
//   setAnalog(&form, true);                                             // keys: <-> step:, the chords stay
//   keyEngineKey("v_push", KeyField::RateMin)                           // "SCROLL_V_PUSH_KEY_RATE_MIN"
//   analogStartValues("v_push", get)                                    // the UI defaults a new analog function starts with
//   checkKeyField(key, text, &err)                                      // what the fields accept
//   triggerPercent(settings, "v_push", 20.0)                            // "about N % of full deflection"
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "modes.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

enum class KeyField { RateMin, RateMax, Trigger };

// The UI defaults of a new analog key function: 3 to 25 presses per second. A function without these keys keeps the
// behavior of older versions (no lower limit, no upper limit below the hard cap).
constexpr double kUiRateMin = 3.0, kUiRateMax = 25.0;

// The key slot of a motion id ("v_push" ... "lift"), -1 for an unknown one.
inline int keyMotionOf(const std::string &motionId) {
    static const char *const ids[KM_COUNT] = {"v_push", "v_tilt", "h_push", "h_tilt", "twist", "lift"};
    for (int i = 0; i < KM_COUNT; ++i)
        if (motionId == ids[i]) return i;
    return -1;
}

// "SCROLL_V_PUSH_KEY_RATE_MIN", ... for the motion; empty for an unknown motion.
inline std::string keyEngineKey(const std::string &motionId, KeyField f) {
    const int m = keyMotionOf(motionId);
    if (m < 0) return std::string();
    return std::string(keyTuningPrefix(m)) + (f == KeyField::RateMin ? "KEY_RATE_MIN" : f == KeyField::RateMax ? "KEY_RATE_MAX" : "TRIGGER");
}

struct AnalogChoice {
    bool shown = false;    // the function presses keys (keys: or step:), so the check box is there
    bool analog = false;   // keys: (checked)
    bool allowed = false;  // analog may be chosen: no tier B key, not the lift
    std::string reason;    // why it is greyed (English source text through the provider); empty when allowed
};

// Is an analog `keys:` allowed for these chords? Reason in words when not. A `none` side is no key.
inline bool analogAllowedFor(const std::string &motionId, const ChannelForm &form, std::string *reason, const StringProvider &s) {
    if (motionId == "lift") {
        if (reason) *reason = s.tr("Lift / press can fire when you grab the cap, so it only presses one key per push.");
        return false;
    }
    ChannelAction a;
    std::string err;
    if (!parseChannelAction("keys:" + form.neg + "," + form.pos, &a, &err)) {
        if (reason) *reason = s.tr("This key can only be used digitally: one press per push. A key that repeats could do damage (letters, digits, Enter, Delete, Esc, Alt or Super combinations).");
        return false;
    }
    return true;
}

inline AnalogChoice analogChoice(const std::string &motionId, const ChannelForm &form, const StringProvider &s) {
    AnalogChoice c;
    c.shown = form.usesKeys();
    c.analog = form.type == ChannelActionType::Keys;
    if (!c.shown) return c;
    c.allowed = analogAllowedFor(motionId, form, &c.reason, s);
    if (c.analog) c.allowed = true;  // already analog: it was accepted once, let the user switch it off
    if (c.allowed) c.reason.clear();
    return c;
}

// The check box: keys: <-> step:. The chords stay as they are. false (and nothing changed) when the form presses no keys.
inline bool setAnalog(ChannelForm *form, bool analog) {
    if (!form->usesKeys()) return false;
    form->type = analog ? ChannelActionType::Keys : ChannelActionType::Step;
    return true;
}

// The values a new analog function starts with: the UI defaults, only for what the motion does not set yet
// (get returns the current text of a key, empty when it is not set). Key / value pairs to store.
inline std::vector<std::pair<std::string, std::string>> analogStartValues(const std::string &motionId,
                                                                          const std::function<std::string(const std::string &)> &get) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string lo = keyEngineKey(motionId, KeyField::RateMin), hi = keyEngineKey(motionId, KeyField::RateMax);
    if (lo.empty() || !get(lo).empty() || !get(hi).empty()) return out;
    out.emplace_back(lo, "3");
    out.emplace_back(hi, "25");
    return out;
}

// What a rate or trigger field accepts: the same check as the file loader. false + *err (English source text).
inline bool checkKeyField(const std::string &key, const std::string &text, std::string *err) {
    Settings probe;
    return applyKey(probe, key, text, err);
}

// The trigger level as "about N % of full deflection", from the channel's sensitivity and dead zone (a straight line: the
// curve is not looked at). 0 when it cannot be told (sensitivity 0, unknown motion).
inline int triggerPercent(const Settings &s, const std::string &motionId, double trigger) {
    const int m = keyMotionOf(motionId);
    if (m < 0 || trigger <= 0.0) return 0;
    const ChannelCfg &c = m == KM_TWIST ? s.zoom : m == KM_LIFT ? s.lift : m <= KM_V_TILT ? s.scroll_v : s.scroll_h;
    if (c.sensitivity <= 0) return 0;
    const double perUnit = c.sensitivity / 2500.0 * 125.0;  // steps per second per raw unit (Channel::processValue)
    const double raw = trigger / perUnit + c.deadzone;
    return int(std::lround(std::min(100.0, raw / 350.0 * 100.0)));
}

// The trigger actually in force for the motion: its own, else STEP_TRIGGER.
inline double effectiveTrigger(const Settings &s, const std::string &motionId) {
    const int m = keyMotionOf(motionId);
    if (m < 0) return s.step_trigger;
    return (s.key_tuning[m].set & KeyTuning::F_TRIGGER) ? s.key_tuning[m].trigger : s.step_trigger;
}

// The words under the check box and the fields, in plain words.
struct KeyEngineWords {
    std::string mode;     // what the chosen kind does
    std::string rateMin;  // label and help of the fields
    std::string rateMax;
    std::string trigger;
    std::string defaults;  // what an empty field means, and the UI defaults
};
inline KeyEngineWords keyEngineWords(bool analog, const Settings &effective, const std::string &motionId, const StringProvider &s) {
    KeyEngineWords w;
    const double t = effectiveTrigger(effective, motionId);
    const int pct = triggerPercent(effective, motionId, t);
    if (analog) {
        w.mode = s.tr("Analog: the key repeats faster the further you push, between the lowest and the highest rate below.");
        w.defaults = s.tr("Empty: no lower limit, and the repeat follows the push (never more than 100 presses a second). A new analog function starts with 3 to 25 presses a second.");
    } else {
        w.mode = s.tr("Digital: one press each time you push past the trigger level, like a click. It presses again after you ease back to half of the level and push again.");
        w.defaults = s.tr("Empty: the level of the whole configuration (STEP_TRIGGER) applies.");
    }
    w.rateMin = s.tr("Lowest rate (presses a second)");
    w.rateMax = s.tr("Highest rate (presses a second)");
    w.trigger = pct > 0 ? format(s.tr("Trigger level (about %1 % of full deflection)"), {std::to_string(pct)}) : s.tr("Trigger level");
    return w;
}

}  // namespace spn::ui
