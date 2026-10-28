// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The models behind the mouse click and hold choices (stage 4 of #224): the function lists, what choosing an entry writes, the
// analog check box that stays greyed with its reason, the words, the action tiles and the stored text of a form.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "ui/model/action_preset_model.hpp"
#include "ui/model/key_engine_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_summary_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct English : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
ChannelForm form(ChannelActionType t, const char *neg, const char *pos) {
    ChannelForm f;
    f.type = t;
    f.neg = neg;
    f.pos = pos;
    return f;
}
}  // namespace

int main() {
    English s;
    // the lists offer both functions in plain words, with unique ids
    for (const auto *list : {&motionActionChoices(), &liftActionChoices()}) {
        std::set<int> ids;
        bool click = false, hold = false;
        for (const MotionActionChoice &c : *list) {
            assert(ids.insert(c.key()).second);
            if (c.key() == kChoiceMouseClick) click = std::string(c.label) == "Mouse click per push";
            if (c.type == ChannelActionType::Hold) hold = std::string(c.label) == "Hold a key or button while pushed";
        }
        assert(click && hold);
    }
    // the entry that shows a form
    assert(choiceIdOf(form(ChannelActionType::Step, "none", "click")) == kChoiceMouseClick);
    assert(choiceIdOf(form(ChannelActionType::Step, "up", "down")) == int(ChannelActionType::Step));
    assert(choiceIdOf(form(ChannelActionType::Step, "up", "click")) == int(ChannelActionType::Step));  // a key is among the sides
    assert(choiceIdOf(form(ChannelActionType::Hold, "none", "click")) == int(ChannelActionType::Hold));
    // choosing an entry writes a text the parser accepts
    ChannelForm f = form(ChannelActionType::Keys, "pageup", "pagedown");
    applyChoice(&f, kChoiceMouseClick);
    assert(f.text() == "step:none,click");
    applyChoice(&f, int(ChannelActionType::Hold));
    assert(f.type == ChannelActionType::Hold && f.text() == "hold:none,click");  // the sides stay
    applyChoice(&f, int(ChannelActionType::Step));
    assert(f.text() == "step:up,down");  // keys after a click start fresh
    applyChoice(&f, int(ChannelActionType::Hold));
    assert(f.text() == "hold:up,down");  // the keys of a step carry over to a hold
    applyChoice(&f, int(ChannelActionType::Keys));
    assert(f.type == ChannelActionType::Keys && f.text() == "keys:up,down");
    // from analog keys to a mouse click: a click is never analog
    applyChoice(&f, kChoiceMouseClick);
    assert(f.type == ChannelActionType::Step && f.anyMouse());
    for (const ChannelForm &g : {form(ChannelActionType::Step, "click", "rightclick"), form(ChannelActionType::Hold, "ctrl+shift", "none"),
                                 form(ChannelActionType::Hold, "none", "middleclick")}) {
        ChannelAction a;
        std::string err;
        assert(parseChannelAction(g.text(), &a, &err));
        assert(channelFormOf(a).text() == g.text());  // the form of a loaded action says the same
    }
    // the analog check box: greyed with its reason for a click and a hold, and never switched on
    for (const ChannelForm &g : {form(ChannelActionType::Hold, "ctrl", "none"), form(ChannelActionType::Step, "none", "click")}) {
        const AnalogChoice c = analogChoice("v_tilt", g, s);
        assert(c.shown && !c.analog && !c.allowed && !c.reason.empty());
        ChannelForm h = g;
        assert(!setAnalog(&h, true) && h.type == g.type);
    }
    assert(analogChoice("v_tilt", form(ChannelActionType::Hold, "ctrl", "none"), s).hold);
    assert(analogChoice("v_tilt", form(ChannelActionType::Hold, "ctrl", "none"), s).reason.find("Holding") != std::string::npos);
    assert(analogChoice("v_tilt", form(ChannelActionType::Step, "none", "click"), s).reason.find("click") != std::string::npos);
    // a key function stays switchable
    ChannelForm k = form(ChannelActionType::Step, "up", "down");
    assert(analogChoice("v_tilt", k, s).allowed && setAnalog(&k, true) && k.type == ChannelActionType::Keys);
    // the words of a hold name the half level and say that it is never let go on a timer
    const KeyEngineWords w = keyEngineWords(false, Settings(), "v_tilt", s, true);
    assert(w.mode.find("Hold") == 0 && w.mode.find("half") != std::string::npos);
    // action tiles
    const std::vector<ActionPreset> tiles = actionPresets(s);
    bool click = false, hold = false;
    for (const ActionPreset &p : tiles) {
        click = click || p.id == "click";
        hold = hold || p.id == "hold";
    }
    assert(click && hold);
    for (Motion m : allMotions()) {
        if (m == Motion::Lift) continue;
        assert(activePreset(m, presetForm("click", m, ChannelForm())) == "click");
        assert(activePreset(m, presetForm("hold", m, ChannelForm())) == "hold");
        assert(activePreset(m, presetForm("taps", m, form(ChannelActionType::Step, "none", "click"))) == "click");
    }
    // plain words for the mouse buttons
    using motion_detail::keyWords;
    assert(keyWords("rightclick") == "Right click" && keyWords("middleclick") == "Middle click" && keyWords("click") == "Click");
    std::puts("test_ui_mouse_hold: all passed");
    return 0;
}
