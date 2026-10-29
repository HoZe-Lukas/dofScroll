// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The "New function..." dialog behind the screen (new_function_model.hpp): the kinds a motion and a button offer (and the
// reason when one is greyed), the action text every kind composes, the analog / digital check box that is greyed with its reason
// for clicks, holds and macros, the rate and trigger fields, the validation by the parsers of the daemon, assignment as soon as
// all fields are valid (and never before), and reading a stored action back into a draft.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "ui/model/new_function_model.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct NfEnglish : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
bool nfHas(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }
std::string nfSection(const SettingsModel &m, size_t i) {
    std::string t;
    for (const KV &kv : m.profiles().sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}
const FunctionKindInfo *nfKind(const std::vector<FunctionKindInfo> &v, FunctionKind k) {
    for (const FunctionKindInfo &i : v)
        if (i.kind == k) return &i;
    return nullptr;
}
std::string nfText(const FunctionTarget &t, const FunctionDraft &d) {
    std::string text, err;
    return composeFunction(t, d, &text, &err) ? text : "!" + err;
}
}  // namespace

int main() {
    useFreshDefaultOf("linux");
    NfEnglish s;
    const FunctionTarget tilt = FunctionTarget::motion("h_tilt"), lift = FunctionTarget::motion("lift"), btn = FunctionTarget::buttonNumber(3);

    // ---- the kinds of a motion and of a button, greyed ones with their reason ----
    {
        const auto m = functionKinds(tilt, s), b = functionKinds(btn, s);
        assert(m.size() == 6 && b.size() == 6);
        for (FunctionKind k : {FunctionKind::Key, FunctionKind::Click, FunctionKind::HoldKey, FunctionKind::Wheel}) assert(nfKind(m, k)->available && nfKind(m, k)->reason.empty());
        assert(nfKind(b, FunctionKind::Key)->available && nfKind(b, FunctionKind::Click)->available);
        for (FunctionKind k : {FunctionKind::HoldKey, FunctionKind::Wheel, FunctionKind::Pointer}) {
            assert(!nfKind(b, k)->available && !nfKind(b, k)->reason.empty());  // a button cannot do these, and says why
        }
        assert(nfHas(nfKind(b, FunctionKind::HoldKey)->reason, "plain key"));
        // macro and pointer light up by themselves as soon as the parsers know them, and carry a reason while they do not
        for (FunctionKind k : {FunctionKind::Macro, FunctionKind::Pointer}) assert(nfKind(m, k)->available || !nfKind(m, k)->reason.empty());
    }

    // ---- composing: one action text per kind ----
    {
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        assert(nfText(tilt, d) == "step:up,down");
        d.neg = "ctrl+pageup";
        d.pos = "ctrl+pagedown";
        assert(nfText(tilt, d) == "step:ctrl+pageup,ctrl+pagedown");
        assert(setDraftAnalog(&d, true) && nfText(tilt, d) == "keys:ctrl+pageup,ctrl+pagedown");
        assert(d.rateMin == "3" && d.rateMax == "25");  // a new analog function starts with the UI defaults
        d.pos = "none";
        assert(nfText(tilt, d) == "keys:ctrl+pageup,none");
        d.neg = "none";
        assert(nfText(tilt, d).compare(0, 1, "!") == 0 && !checkFunction(tilt, d).empty());  // both sides without a key
        d = newDraft(tilt, FunctionKind::Click);
        assert(nfText(tilt, d) == "step:none,click");
        d.neg = "rightclick";
        assert(nfText(tilt, d) == "step:rightclick,click");
        d = newDraft(tilt, FunctionKind::HoldKey);
        assert(nfText(tilt, d) == "hold:none,click");
        d.neg = "shift";
        assert(nfText(tilt, d) == "hold:shift,click");
        d = newDraft(tilt, FunctionKind::Wheel);
        assert(nfText(tilt, d) == "wheel_v");
        d.wheel = "zoom";
        assert(nfText(tilt, d) == "zoom");
        d.wheel = "bogus";
        assert(!checkFunction(tilt, d).empty());
        d = newDraft(btn, FunctionKind::Key);
        assert(nfText(btn, d) == "key:space");
        d.neg = "ctrl+0";
        assert(nfText(btn, d) == "key:ctrl+0");
        d = newDraft(btn, FunctionKind::Click);
        assert(nfText(btn, d) == "click");
        d.neg = "middleclick";
        assert(nfText(btn, d) == "middleclick");
        d.neg = "none";
        assert(!checkFunction(btn, d).empty());
        d = newDraft(btn, FunctionKind::HoldKey);
        assert(!checkFunction(btn, d).empty());  // not for a button
        d = newDraft(btn, FunctionKind::Wheel);
        assert(!checkFunction(btn, d).empty());
        // a macro: the text is built from the steps; the parsers decide whether this version knows it
        d = newDraft(tilt, FunctionKind::Macro);
        assert(nfText(tilt, d).compare(0, 1, "!") == 0);  // no step yet
        d.macroPos.add(MacroStep::down("ctrl"));
        d.macroPos.add(MacroStep::down("c"));
        d.macroPos.add(MacroStep::up("c"));
        assert(nfText(tilt, d) == "macro:none,+ctrl +c -c");
        d.macroNeg.add(MacroStep::wait(30));
        assert(nfText(tilt, d) == "macro:wait:30,+ctrl +c -c");
        d = newDraft(btn, FunctionKind::Macro);
        d.macroNeg.add(MacroStep::down("shift"));
        assert(nfText(btn, d) == "macro:+shift");
    }

    // ---- the analog check box: only the key function of a motion may use it, the rest is greyed with the reason ----
    {
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        d.neg = "pageup";
        d.pos = "pagedown";
        FunctionAnalog a = functionAnalog(tilt, d, s);
        assert(a.allowed && !a.checked && a.reason.empty());
        d.pos = "x";  // a letter can only be used digitally
        a = functionAnalog(tilt, d, s);
        assert(!a.allowed && nfHas(a.reason, "digitally"));
        d.pos = "pagedown";
        assert(functionAnalog(lift, d, s).allowed == false && nfHas(functionAnalog(lift, d, s).reason, "Lift"));
        assert(nfHas(functionAnalog(tilt, newDraft(tilt, FunctionKind::Click), s).reason, "mouse click"));
        assert(nfHas(functionAnalog(tilt, newDraft(tilt, FunctionKind::HoldKey), s).reason, "Holding"));
        assert(functionAnalog(tilt, newDraft(tilt, FunctionKind::Macro), s).reason == "A macro always runs once per trigger.");
        assert(!functionAnalog(tilt, newDraft(tilt, FunctionKind::Wheel), s).allowed);
        assert(nfHas(functionAnalog(btn, newDraft(btn, FunctionKind::Key), s).reason, "always digital"));
        FunctionDraft click = newDraft(tilt, FunctionKind::Click);
        assert(!setDraftAnalog(&click, true));
    }

    // ---- which fields a kind shows ----
    {
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        assert(!functionFields(tilt, d).rates && functionFields(tilt, d).trigger);
        setDraftAnalog(&d, true);
        assert(functionFields(tilt, d).rates && functionFields(tilt, d).trigger);
        assert(!functionFields(tilt, newDraft(tilt, FunctionKind::Wheel)).trigger);
        assert(functionFields(tilt, newDraft(tilt, FunctionKind::HoldKey)).trigger && functionFields(tilt, newDraft(tilt, FunctionKind::Macro)).trigger);
        assert(!functionFields(btn, newDraft(btn, FunctionKind::Key)).trigger);
    }

    // ---- switching the kind keeps what fits ----
    {
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        d.neg = "ctrl+pageup";
        d.pos = "ctrl+pagedown";
        changeKind(&d, tilt, FunctionKind::HoldKey);
        assert(d.kind == FunctionKind::HoldKey && d.neg == "ctrl+pageup" && d.pos == "ctrl+pagedown");
        changeKind(&d, tilt, FunctionKind::Click);
        assert(d.neg == "none" && d.pos == "click");  // keys do not become clicks
        changeKind(&d, tilt, FunctionKind::HoldKey);
        assert(d.neg == "none" && d.pos == "click");
        changeKind(&d, tilt, FunctionKind::Key);
        assert(d.neg == "up" && d.pos == "down");  // a mouse side makes way
    }

    // ---- reading a stored action back ----
    {
        bool fresh = false;
        auto none = [](const std::string &) { return std::string(); };
        FunctionDraft d = draftFromText(tilt, "keys:ctrl+pageup,ctrl+pagedown", "h_push", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Key && d.analog && d.neg == "ctrl+pageup" && d.pos == "ctrl+pagedown");
        d = draftFromText(tilt, "step:none,click", "h_push", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Click && d.neg == "none" && d.pos == "click");
        d = draftFromText(tilt, "hold:shift,click", "h_push", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::HoldKey && d.neg == "shift");
        d = draftFromText(tilt, "zoom", "h_push", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Wheel && d.wheel == "zoom");
        d = draftFromText(tilt, "default", "h_push", none, &fresh);
        assert(fresh && d.kind == FunctionKind::Key);
        d = draftFromText(tilt, "none", "h_push", none, &fresh);
        assert(fresh);
        d = draftFromText(btn, "key:ctrl+0", "", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Key && d.neg == "ctrl+0");
        d = draftFromText(btn, "rightclick", "", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Click && d.neg == "rightclick");
        d = draftFromText(btn, "mode:next", "", none, &fresh);
        assert(fresh);  // a mode switch is no function of this dialog
        d = draftFromText(btn, "macro:+ctrl +c -c -ctrl", "", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Macro && d.macroNeg.size() == 4);
        d = draftFromText(tilt, "macro:none,wait:30 +a", "h_push", none, &fresh);
        assert(!fresh && d.kind == FunctionKind::Macro && d.macroNeg.empty() && d.macroPos.size() == 2);
        // the tuning keys of the motion are read with the action
        d = draftFromText(tilt, "keys:left,right", "h_push", [](const std::string &k) { return k == "SCROLL_H_PUSH_KEY_RATE_MAX" ? std::string("8") : std::string(); }, &fresh);
        assert(d.rateMax == "8" && d.rateMin.empty());
    }

    // ---- assignment as soon as all fields are valid, never before ----
    {
        ModeSet modes = loadModes("/nonexistent-dir-for-test", nullptr);
        ProfileSet ps;
        const size_t app = ps.findOrAdd(Section::APP, "browser");
        SettingsModel m;
        m.setProfiles(ps, "");
        m.setModes(modes, "");
        std::vector<FunctionProblem> pr;
        // tab switching, analog on the sideways tilt
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        d.neg = "ctrl+pageup";
        d.pos = "ctrl+pagedown";
        setDraftAnalog(&d, true);
        d.rateMax = "8";
        assert(applyFunction(m, app, tilt, d, &pr) && pr.empty());
        assert(nfSection(m, app) == "H_TILT_ACTION=keys:ctrl+pageup,ctrl+pagedown;SCROLL_H_PUSH_KEY_RATE_MIN=3;SCROLL_H_PUSH_KEY_RATE_MAX=8;" ||
               nfSection(m, app) == "H_TILT_ACTION=keys:ctrl+pageup,ctrl+pagedown;SCROLL_H_TILT_KEY_RATE_MIN=3;SCROLL_H_TILT_KEY_RATE_MAX=8;");
        // an invalid field is shown and nothing changes
        const std::string before = nfSection(m, app);
        FunctionDraft bad = d;
        bad.rateMin = "abc";
        assert(!applyFunction(m, app, tilt, bad, &pr) && pr.size() == 1 && pr[0].field == "rate_min" && nfSection(m, app) == before);
        bad = d;
        bad.rateMin = "30";
        bad.rateMax = "5";
        assert(!applyFunction(m, app, tilt, bad, &pr) && pr[0].field == "rate_min" && nfHas(pr[0].message, "higher") && nfSection(m, app) == before);
        // a letter cannot repeat: the parser refuses analog, nothing is written
        bad = d;
        bad.neg = "a";
        assert(!applyFunction(m, app, tilt, bad, &pr) && pr[0].field == "action" && nfSection(m, app) == before);
        // digital: the trigger field
        d.analog = false;
        d.trigger = "30";
        assert(applyFunction(m, app, tilt, d, &pr));
        assert(nfHas(nfSection(m, app), "H_TILT_ACTION=step:ctrl+pageup,ctrl+pagedown;") && nfHas(nfSection(m, app), "TRIGGER=30;"));
        // a click on the lift, a button with a key, a button with a click
        FunctionDraft c = newDraft(lift, FunctionKind::Click);
        assert(applyFunction(m, app, lift, c, &pr) && nfHas(nfSection(m, app), "LIFT_ACTION=step:none,click;"));
        FunctionDraft k = newDraft(btn, FunctionKind::Key);
        k.neg = "ctrl+0";
        assert(applyFunction(m, app, btn, k, &pr) && nfHas(nfSection(m, app), "BUTTON_3=key:ctrl+0;"));
        FunctionDraft r = newDraft(btn, FunctionKind::Click);
        r.neg = "rightclick";
        assert(applyFunction(m, app, btn, r, &pr) && nfHas(nfSection(m, app), "BUTTON_3=rightclick;"));
        // an analog key on the lift is refused (it fires when the cap is grabbed)
        FunctionDraft la = newDraft(lift, FunctionKind::Key);
        la.neg = "pageup";
        la.pos = "pagedown";
        la.analog = true;
        assert(!applyFunction(m, app, lift, la, &pr) && pr[0].field == "action");
        // a button above the device's count is refused by the model
        assert(!applyFunction(m, app, FunctionTarget::buttonNumber(99), k, &pr));
        // the macro: written when this version knows it, refused with the reason when not
        FunctionDraft mac = newDraft(btn, FunctionKind::Macro);
        mac.macroNeg.add(MacroStep::down("ctrl"));
        mac.macroNeg.add(MacroStep::down("a"));
        mac.macroNeg.add(MacroStep::up("a"));
        const bool known = nfKind(functionKinds(btn, s), FunctionKind::Macro)->available;
        assert(applyFunction(m, app, btn, mac, &pr) == known);
        if (known) assert(nfHas(nfSection(m, app), "BUTTON_3=macro:+ctrl +a -a;"));
        else assert(nfHas(nfSection(m, app), "BUTTON_3=rightclick;"));
    }

    // ---- the words under the preview ----
    assert(nfHas(functionSummary(tilt, newDraft(tilt, FunctionKind::Key), s), "trigger level"));
    assert(nfHas(functionSummary(btn, newDraft(btn, FunctionKind::Key), s), "space"));

    std::puts("test_ui_new_function: ok");
    return 0;
}
