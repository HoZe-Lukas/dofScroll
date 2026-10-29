// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The "New function..." dialog of the Motions and buttons tab (stage 7a of docs/plan-motion-functions.md), toolkit-free. A
// new function is a composition that ends in exactly one action text of a motion (V_PUSH_ACTION ... LIFT_ACTION) or of a
// device button (BUTTON_<n>), validated by parseChannelAction / parseLiftAction / parseButtonAction like everything else. The
// dialog has no Save button: every edit that leaves all fields valid is assigned at once (applyFunction), an invalid state is
// shown next to its field and never written.
//
// Kinds: a key or key combination, a mouse click, hold a key, scroll or zoom, move the pointer (once the pointer actions
// exist) and a macro (once the daemon knows `macro:`). What a target cannot do is greyed with the reason (functionKinds).
// The analog / digital choice exists for the key function of a motion; for every other kind the check box is greyed with the
// reason (functionAnalog). The rate and trigger fields are the keys <P>KEY_RATE_MIN / KEY_RATE_MAX / TRIGGER of the motion.
//
//   FunctionDraft d = newDraft(target, FunctionKind::Key);   // pageup / pagedown, digital
//   d.neg = "ctrl+pageup"; d.pos = "ctrl+pagedown"; d.analog = true;
//   composeFunction(target, d, &text, &err);                  // "keys:ctrl+pageup,ctrl+pagedown"
//   applyFunction(model, section, target, d, strings, &problems);  // validated, then written with its tuning keys
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "modes.hpp"
#include "ui/model/binding_editor_model.hpp"
#include "ui/model/key_engine_model.hpp"
#include "ui/model/macro_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

enum class FunctionKind { Key, Click, HoldKey, Wheel, Pointer, Macro };

// The drop-down entry "New function..." of a motion or button row: not a choice of its own, it opens the dialog.
constexpr int kChoiceNewFunction = 900;

// Where the new function goes: one of the six motions ("v_push" ... "lift") or a device button (1-based).
struct FunctionTarget {
    std::string motionId;  // empty for a button
    int button = 0;
    static FunctionTarget motion(const std::string &id) {
        FunctionTarget t;
        t.motionId = id;
        return t;
    }
    static FunctionTarget buttonNumber(int n) {
        FunctionTarget t;
        t.button = n;
        return t;
    }
    bool isMotion() const { return !motionId.empty(); }
};

struct FunctionKindInfo {
    FunctionKind kind = FunctionKind::Key;
    std::string label;      // translated
    bool available = true;
    std::string reason;     // translated; why it is greyed
};

inline const char *functionKindId(FunctionKind k) {
    switch (k) {
        case FunctionKind::Key: return "key";
        case FunctionKind::Click: return "click";
        case FunctionKind::HoldKey: return "hold";
        case FunctionKind::Wheel: return "wheel";
        case FunctionKind::Pointer: return "pointer";
        case FunctionKind::Macro: return "macro";
    }
    return "key";
}

namespace new_function_detail {
// Does this version of dofscroll know the action text? The kinds light up by themselves when the parsers learn them.
inline bool macroKnown() {
    ChannelAction a;
    ButtonActionType t;
    Key k;
    return parseChannelAction("macro:+a -a,none", &a, nullptr) && parseButtonAction("macro:+a -a", &t, &k, nullptr);
}
inline bool pointerKnown() {
    ChannelAction a;
    return parseChannelAction("pointer_x", &a, nullptr);
}
inline bool mouseWord(const std::string &s) { return ChannelForm::isMouseWord(s); }
}  // namespace new_function_detail

inline std::vector<FunctionKindInfo> functionKinds(const FunctionTarget &t, const StringProvider &s) {
    std::vector<FunctionKindInfo> out;
    auto add = [&](FunctionKind k, const char *label, bool ok, const std::string &why) {
        FunctionKindInfo i;
        i.kind = k;
        i.label = s.tr(label);
        i.available = ok;
        i.reason = ok ? std::string() : why;
        out.push_back(i);
    };
    const bool motion = t.isMotion();
    add(FunctionKind::Key, "Key or key combination", true, "");
    add(FunctionKind::Click, "Mouse click", true, "");
    add(FunctionKind::HoldKey, "Hold a key or button", motion,
        s.tr("A button holds a plain key already while you hold the button: choose Key or key combination without a modifier."));
    add(FunctionKind::Macro, "Macro", new_function_detail::macroKnown(),
        s.tr("This version of dofscroll does not know macros yet."));
    add(FunctionKind::Wheel, "Scroll or zoom", motion, s.tr("A button cannot scroll or zoom: those need a motion of the cap."));
    add(FunctionKind::Pointer, "Move the pointer", motion && new_function_detail::pointerKnown(),
        motion ? s.tr("This version of dofscroll cannot move the pointer from a motion yet.") : s.tr("A button cannot move the pointer: that needs a motion of the cap."));
    return out;
}

struct FunctionDraft {
    FunctionKind kind = FunctionKind::Key;
    // Key, Click, Hold: what each direction does ("ctrl+pageup", "none", "click", ...). A button uses `neg` only.
    std::string neg = "up", pos = "down";
    bool analog = false;                       // Key on a motion: keys: instead of step:
    std::string wheel = "wheel_v";             // Wheel: wheel_v, wheel_h or zoom
    std::string pointer = "pointer_x";         // Pointer: pointer_x or pointer_y
    MacroSteps macroNeg, macroPos;             // Macro (a button uses macroNeg only)
    std::string rateMin, rateMax, trigger;     // the tuning keys of the motion, "" = not set
};

struct FunctionProblem {
    std::string field;    // "action", "neg", "pos", "rate_min", "rate_max", "trigger"
    std::string message;  // English source text
};

// A draft of a kind with sensible first values (the ones applyChoice uses for the same kinds).
inline FunctionDraft newDraft(const FunctionTarget &t, FunctionKind kind) {
    FunctionDraft d;
    d.kind = kind;
    switch (kind) {
        case FunctionKind::Key:
            d.neg = t.isMotion() ? "up" : "space";
            d.pos = "down";
            break;
        case FunctionKind::Click:
        case FunctionKind::HoldKey:
            d.neg = t.isMotion() ? "none" : "click";
            d.pos = "click";
            break;
        default: break;
    }
    return d;
}

// Switches the kind of a draft; the sides stay when the new kind can use them, else the new kind starts fresh.
inline void changeKind(FunctionDraft *d, const FunctionTarget &t, FunctionKind kind) {
    if (d->kind == kind) return;
    const FunctionDraft before = *d;
    *d = newDraft(t, kind);
    d->rateMin = before.rateMin;
    d->rateMax = before.rateMax;
    d->trigger = before.trigger;
    d->macroNeg = before.macroNeg;
    d->macroPos = before.macroPos;
    d->wheel = before.wheel;
    d->pointer = before.pointer;
    const bool sides = before.kind == FunctionKind::Key || before.kind == FunctionKind::Click || before.kind == FunctionKind::HoldKey;
    if (!sides || !t.isMotion()) return;
    const bool beforeMouse = new_function_detail::mouseWord(before.neg) || new_function_detail::mouseWord(before.pos);
    const bool onlyMouse = (new_function_detail::mouseWord(before.neg) || before.neg == "none") && (new_function_detail::mouseWord(before.pos) || before.pos == "none");
    const bool keep = kind == FunctionKind::HoldKey ? true : kind == FunctionKind::Key ? !beforeMouse : kind == FunctionKind::Click ? onlyMouse : false;
    if (keep) {
        d->neg = before.neg;
        d->pos = before.pos;
    }
}

inline std::string macroSidesText(const FunctionTarget &t, const FunctionDraft &d) {
    if (!t.isMotion()) return d.macroNeg.text();
    const std::string n = d.macroNeg.empty() ? "none" : d.macroNeg.text(), p = d.macroPos.empty() ? "none" : d.macroPos.text();
    return n + "," + p;
}

// The action text of the draft. false + *err (English source text) when the draft is incomplete.
inline bool composeFunction(const FunctionTarget &t, const FunctionDraft &d, std::string *text, std::string *err) {
    auto fail = [&](const char *why) {
        if (err) *err = why;
        return false;
    };
    const bool motion = t.isMotion();
    switch (d.kind) {
        case FunctionKind::Key:
            if (motion) {
                if (d.neg.empty() || d.pos.empty()) return fail("choose a key for each direction, or tick No key");
                if (d.neg == "none" && d.pos == "none") return fail("at least one direction needs a key");
                *text = (d.analog ? "keys:" : "step:") + d.neg + "," + d.pos;
            } else {
                if (d.neg.empty() || d.neg == "none") return fail("choose a key first");
                *text = "key:" + d.neg;
            }
            return true;
        case FunctionKind::Click:
            if (motion) {
                if (d.neg.empty() || d.pos.empty()) return fail("choose a mouse button for each direction, or Nothing");
                if (d.neg == "none" && d.pos == "none") return fail("at least one direction needs a mouse button");
                *text = "step:" + d.neg + "," + d.pos;
            } else {
                if (!new_function_detail::mouseWord(d.neg)) return fail("choose a mouse button first");
                *text = d.neg;
            }
            return true;
        case FunctionKind::HoldKey:
            if (!motion) return fail("a button holds a plain key already: use Key or key combination");
            if (d.neg.empty() || d.pos.empty()) return fail("choose a key or button for each direction, or Nothing");
            if (d.neg == "none" && d.pos == "none") return fail("at least one direction needs a key or button");
            *text = "hold:" + d.neg + "," + d.pos;
            return true;
        case FunctionKind::Wheel:
            if (!motion) return fail("a button cannot scroll or zoom");
            if (d.wheel != "wheel_v" && d.wheel != "wheel_h" && d.wheel != "zoom") return fail("choose scroll, scroll sideways or zoom");
            *text = d.wheel;
            return true;
        case FunctionKind::Pointer:
            if (!motion) return fail("a button cannot move the pointer");
            if (d.pointer != "pointer_x" && d.pointer != "pointer_y") return fail("choose the sideways or the up and down movement");
            *text = d.pointer;
            return true;
        case FunctionKind::Macro:
            if (d.macroNeg.empty() && (!motion || d.macroPos.empty())) return fail("record or add at least one step");
            *text = "macro:" + macroSidesText(t, d);
            return true;
    }
    return fail("unknown kind");
}

namespace new_function_detail {
// The action text through the parser of its place: the motion's (the lift's has no analog keys) or the button's.
inline bool parses(const FunctionTarget &t, const std::string &text, std::string *err) {
    if (t.isMotion()) {
        ChannelAction a;
        return t.motionId == "lift" ? parseLiftAction(text, &a, err) : parseChannelAction(text, &a, err);
    }
    ButtonActionType type;
    Key key;
    return parseButtonAction(text, &type, &key, err);
}
inline bool numberField(const std::string &key, const std::string &text, std::string *err) {
    return text.empty() || checkKeyField(key, text, err);
}
}  // namespace new_function_detail

// Which tuning fields the draft shows: the lowest and highest rate for the analog key, the trigger level for every digital
// function of a motion (nothing for scroll, zoom and the pointer, and for a button).
struct FunctionFields {
    bool rates = false, trigger = false;
};
inline FunctionFields functionFields(const FunctionTarget &t, const FunctionDraft &d) {
    FunctionFields f;
    if (!t.isMotion()) return f;
    switch (d.kind) {
        case FunctionKind::Key:
            f.rates = d.analog;
            f.trigger = true;
            break;
        case FunctionKind::Click:
        case FunctionKind::HoldKey:
        case FunctionKind::Macro: f.trigger = true; break;
        default: break;
    }
    return f;
}

// What is wrong with the draft, field by field; empty = every field is valid. The key fields of a motion are checked by the
// parser of the whole action ("action"); the rate and trigger fields are checked by the loader's own check.
inline std::vector<FunctionProblem> checkFunction(const FunctionTarget &t, const FunctionDraft &d) {
    std::vector<FunctionProblem> out;
    std::string text, err;
    if (!composeFunction(t, d, &text, &err)) out.push_back({"action", err});
    else if (!new_function_detail::parses(t, text, &err)) out.push_back({"action", err});
    if (!t.isMotion()) return out;
    const FunctionFields fields = functionFields(t, d);
    struct F {
        const char *field;
        KeyField kf;
        bool shown;
        const std::string *value;
    };
    const F list[] = {{"rate_min", KeyField::RateMin, fields.rates, &d.rateMin},
                      {"rate_max", KeyField::RateMax, fields.rates, &d.rateMax},
                      {"trigger", KeyField::Trigger, fields.trigger, &d.trigger}};
    bool numbers = true;
    for (const F &f : list) {
        if (!f.shown || f.value->empty()) continue;
        if (!checkKeyField(keyEngineKey(t.motionId, f.kf), *f.value, &err)) {
            out.push_back({f.field, err});
            numbers = false;
        }
    }
    if (numbers && fields.rates && !d.rateMin.empty() && !d.rateMax.empty() && std::stod(d.rateMin) > std::stod(d.rateMax))
        out.push_back({"rate_min", "the lowest rate is higher than the highest"});
    return out;
}

// The analog / digital check box of the dialog for a draft: `shown` always (the box is part of the dialog), `allowed` only for
// the key function of a motion whose keys may repeat; `reason` says why it is greyed.
struct FunctionAnalog {
    bool checked = false;
    bool allowed = false;
    std::string reason;
};
inline FunctionAnalog functionAnalog(const FunctionTarget &t, const FunctionDraft &d, const StringProvider &s) {
    FunctionAnalog a;
    if (!t.isMotion()) {
        a.reason = s.tr("A button is always digital: one press per press of the button.");
        return a;
    }
    switch (d.kind) {
        case FunctionKind::Key: {
            ChannelForm f;
            f.type = d.analog ? ChannelActionType::Keys : ChannelActionType::Step;
            f.neg = d.neg;
            f.pos = d.pos;
            const AnalogChoice c = analogChoice(t.motionId, f, s);
            a.checked = d.analog;
            a.allowed = c.allowed;
            a.reason = c.reason;
            return a;
        }
        case FunctionKind::Click: a.reason = s.tr("A mouse click can only be used digitally: one click per push."); break;
        case FunctionKind::HoldKey: a.reason = s.tr("Holding is always digital: the key or button is held while you push past the trigger level and let go when you ease back to half of it."); break;
        case FunctionKind::Macro: a.reason = s.tr("A macro always runs once per trigger."); break;
        case FunctionKind::Wheel:
        case FunctionKind::Pointer: a.reason = s.tr("Scrolling, zooming and moving the pointer follow how far you push: they are always analog."); break;
    }
    return a;
}

// Switches the key function between analog and digital; a new analog function starts with the UI defaults (3 to 25 a second).
inline bool setDraftAnalog(FunctionDraft *d, bool analog) {
    if (d->kind != FunctionKind::Key) return false;
    d->analog = analog;
    if (analog && d->rateMin.empty() && d->rateMax.empty()) {
        d->rateMin = "3";
        d->rateMax = "25";
    }
    return true;
}

// The words under the preview: what the function does, in plain words (a short sentence, English source text).
inline std::string functionSummary(const FunctionTarget &t, const FunctionDraft &d, const StringProvider &s) {
    switch (d.kind) {
        case FunctionKind::Key:
            if (!t.isMotion()) return format(s.tr("Pressing the button presses %1."), {d.neg});
            return d.analog ? s.tr("The key repeats faster the further you push.") : s.tr("One key press each time you push past the trigger level.");
        case FunctionKind::Click: return s.tr("A mouse click each time you push past the trigger level.");
        case FunctionKind::HoldKey: return s.tr("The key or button is held while you push past the trigger level.");
        case FunctionKind::Wheel: return s.tr("Follows how far you push.");
        case FunctionKind::Pointer: return s.tr("Moves the pointer by how far you push.");
        case FunctionKind::Macro: return s.tr("The steps run once for each trigger; further triggers during a run are ignored.");
    }
    return std::string();
}

// The draft that shows an action text (the text a motion or button has now), and the tuning keys it holds. `fresh` (optional)
// is set when the text is no function of this dialog ("default", "none", unset): the draft is then a new one.
inline FunctionDraft draftFromText(const FunctionTarget &t, const std::string &textIn, const std::string &tuningMotion,
                                   const std::function<std::string(const std::string &)> &tuning, bool *fresh = nullptr) {
    FunctionDraft d = newDraft(t, FunctionKind::Key);
    if (fresh) *fresh = true;
    std::string text = textIn;
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
    text = utf8::lowerAscii(text);
    if (t.isMotion() && tuning) {
        d.rateMin = tuning(keyEngineKey(tuningMotion, KeyField::RateMin));
        d.rateMax = tuning(keyEngineKey(tuningMotion, KeyField::RateMax));
        d.trigger = tuning(keyEngineKey(tuningMotion, KeyField::Trigger));
    }
    if (text.compare(0, 6, "macro:") == 0) {
        d.kind = FunctionKind::Macro;
        const std::string body = text.substr(6);
        if (fresh) *fresh = false;
        if (t.isMotion()) {
            const size_t comma = body.find(',');
            const std::string n = body.substr(0, comma), p = comma == std::string::npos ? std::string("none") : body.substr(comma + 1);
            if (n != "none") d.macroNeg.parse(n, nullptr);
            if (p != "none") d.macroPos.parse(p, nullptr);
        } else {
            d.macroNeg.parse(body, nullptr);
        }
        return d;
    }
    if (t.isMotion()) {
        if (text == "pointer_x" || text == "pointer_y") {
            d.kind = FunctionKind::Pointer;
            d.pointer = text;
            if (fresh) *fresh = false;
            return d;
        }
        ChannelAction a;
        if (!parseChannelAction(text, &a, nullptr)) return d;
        const ChannelForm f = channelFormOf(a);
        switch (a.type) {
            case ChannelActionType::WheelV: d.kind = FunctionKind::Wheel; d.wheel = "wheel_v"; break;
            case ChannelActionType::WheelH: d.kind = FunctionKind::Wheel; d.wheel = "wheel_h"; break;
            case ChannelActionType::Zoom: d.kind = FunctionKind::Wheel; d.wheel = "zoom"; break;
            case ChannelActionType::Keys: d.kind = FunctionKind::Key; d.analog = true; d.neg = f.neg; d.pos = f.pos; break;
            case ChannelActionType::Step:
                d.kind = f.anyMouse() && !f.anyKeySide() ? FunctionKind::Click : FunctionKind::Key;
                d.neg = f.neg;
                d.pos = f.pos;
                break;
            case ChannelActionType::Hold: d.kind = FunctionKind::HoldKey; d.neg = f.neg; d.pos = f.pos; break;
            default: return d;
        }
        if (fresh) *fresh = false;
        return d;
    }
    Binding b;
    if (!parseBinding(text, &b)) return d;
    switch (b.kind) {
        case BindingKind::Key: d.kind = FunctionKind::Key; d.neg = b.arg; break;
        case BindingKind::Click: d.kind = FunctionKind::Click; d.neg = "click"; break;
        case BindingKind::RightClick: d.kind = FunctionKind::Click; d.neg = "rightclick"; break;
        case BindingKind::MiddleClick: d.kind = FunctionKind::Click; d.neg = "middleclick"; break;
        default: return d;
    }
    if (fresh) *fresh = false;
    return d;
}

// Validates the draft and, when every field is valid, assigns it: the action text of the motion or button of the profile
// `section`, and the tuning keys of a key function (a field left empty removes its key). Nothing is written when anything is
// invalid. false + *problems otherwise (also when the joystick mode of the profile keeps the motion).
inline bool applyFunction(SettingsModel &m, size_t section, const FunctionTarget &t, const FunctionDraft &d,
                          std::vector<FunctionProblem> *problems) {
    std::vector<FunctionProblem> local;
    std::vector<FunctionProblem> &out = problems ? *problems : local;
    out = checkFunction(t, d);
    if (!out.empty()) return false;
    std::string text, err;
    composeFunction(t, d, &text, &err);
    if (!t.isMotion()) {
        if (!m.setButton(section, t.button, text, &err)) {
            out.push_back({"action", err});
            return false;
        }
        return true;
    }
    if (!motion_edit_detail::writeAction(m, section, t.motionId, motion_edit_detail::partKeyOf(t.motionId), text, &err)) {
        out.push_back({"action", err});
        return false;
    }
    const FunctionFields fields = functionFields(t, d);
    const std::string motion = motion_edit_detail::keyTuningMotion(m, section, t.motionId);
    auto put = [&](KeyField kf, const std::string &value) {
        const std::string key = keyEngineKey(motion, kf);
        if (key.empty()) return;
        if (value.empty()) m.clearOverride(section, key);
        else m.setValue(section, key, value, nullptr);
    };
    if (fields.rates) {
        put(KeyField::RateMin, d.rateMin);
        put(KeyField::RateMax, d.rateMax);
    }
    if (fields.trigger) put(KeyField::Trigger, d.trigger);
    return true;
}

}  // namespace spn::ui
