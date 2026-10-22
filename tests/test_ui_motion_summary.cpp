// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The six motions of the cap as the Joystick modes page shows them: every built-in joystick mode gives
// six rows in a fixed order with the expected actions, split channels give push and tilt their own
// action, a mix override changes what is "Not used", the strings go through the StringProvider.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "testutil.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/motion_summary_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
class Marking : public StringProvider {  // marks every string as translated
public:
    std::string tr(const char *source) const override { return std::string("<") + source + ">"; }
};

// chips of the rows in the order v_push v_tilt h_push h_tilt lift twist
std::vector<std::string> chips(const ModeDef &m) {
    IdentityStrings id;
    std::vector<std::string> out;
    for (const MotionRow &r : motionSummary(m, id)) out.push_back(r.chip);
    return out;
}
}  // namespace

int main() {
    IdentityStrings id;
    const ModeSet builtins = loadModes("/nonexistent-dir-for-test", nullptr);

    // every built-in joystick mode: vertical chip, horizontal chip, twist chip (lift is never used)
    struct Expect {
        const char *id, *v, *h, *t;
    };
    const Expect table[] = {{"normal", "Scroll", "Sideways", "Zoom"},     {"off", "Off", "Off", "Off"},
                            {"timeline", "Off", "Keys", "Keys"},          {"slides", "Keys", "Off", "Off"},
                            {"media", "Keys", "Keys", "Keys"},            {"map", "Keys", "Keys", "Keys"},
                            {"precision", "Scroll", "Sideways", "Zoom"},  {"tabs", "Scroll", "Keys", "Zoom"},
                            {"browse", "Keys", "Keys", "Zoom"},           {"sheet", "Keys", "Keys", "Keys"},
                            {"photo", "Zoom", "Keys", "Off"},             {"zoom-pan", "Zoom", "Sideways", "Sideways"},
                            {"vertical-only", "Scroll", "Off", "Off"},    {"fast", "Scroll", "Sideways", "Zoom"}};
    for (const Expect &e : table) {
        const ModeDef *m = builtins.find(e.id);
        assert(m && "built-in joystick mode is there");
        const std::vector<MotionRow> rows = motionSummary(*m, id);
        assert(rows.size() == 6);
        const char *ids[6] = {"v_push", "v_tilt", "h_push", "h_tilt", "lift", "twist"};
        for (size_t i = 0; i < 6; ++i) assert(rows[i].id == ids[i] && !rows[i].name.empty() && !rows[i].how.empty());
        assert(rows[0].chip == e.v && rows[1].chip == e.v);  // unsplit: push and tilt agree
        assert(rows[2].chip == e.h && rows[3].chip == e.h);
        assert(rows[5].chip == e.t);
        assert(rows[4].kind == MotionKind::NotUsed && rows[4].action == "Not used" && rows[4].chip == "Unused" && rows[4].muted());
        for (size_t i = 0; i < 6; ++i)
            if (i != 4) assert(rows[i].muted() == (rows[i].kind == MotionKind::Off) && !rows[i].action.empty());
        assert(motionChipTexts(rows).size() == 6);
    }

    // the texts
    {
        const std::vector<MotionRow> n = motionSummary(*builtins.find("normal"), id);
        assert(n[0].name == "Vertical push" && n[0].action == "Scroll" && n[0].symbol == "scroll");
        assert(n[2].action == "Scroll sideways" && n[2].symbol == "scroll_h" && n[5].action == "Zoom: Ctrl + mouse wheel" && n[5].symbol == "zoom");
        // zoom says how it zooms: a Keys action (Ctrl+Plus / Ctrl+Minus) is a different zoom
        assert(n[5].detail == "Ctrl+Wheel" && motionChipTexts(n)[5] == motionMarker("twist") + " " + motionGlyph("zoom") + " Ctrl+Wheel");
        // the keys come from ZOOM_MODIFIER of the settings (a profile can differ, e.g. Cmd on macOS)
        Settings mac;
        mac.zoom_modifier = kZoomModSuper;
        assert(motionSummary(*builtins.find("normal"), mac, id)[5].detail == "Super+Wheel");
        mac.zoom_modifier = kZoomModCtrl | kZoomModShift;
        assert(motionSummary(*builtins.find("normal"), mac, id)[5].action == "Zoom: Ctrl+Shift + mouse wheel");
        mac.zoom_modifier = 0;
        assert(motionSummary(*builtins.find("normal"), mac, id)[5].detail == "Wheel");
        ModeDef alt = *builtins.find("normal");  // a mode may override it as well
        alt.overrides.push_back({"ZOOM_MODIFIER", "alt"});
        assert(motionSummary(alt, id)[5].detail == "Alt+Wheel");
        assert(n[1].name == "Vertical tilt" && n[3].name == "Horizontal tilt" && n[5].name == "Twist" && n[4].name == "Lift / press");
        const std::vector<MotionRow> t = motionSummary(*builtins.find("tabs"), id);
        assert(t[2].action == "Keys: Ctrl+Page up / Ctrl+Page down" && t[2].symbol == "keys");
        const std::vector<MotionRow> o = motionSummary(*builtins.find("off"), id);
        assert(o[0].action == "Off" && o[0].kind == MotionKind::Off && o[0].symbol == "off" && o[0].muted());
        assert(motionChipTexts(n)[0] == motionMarker("v_push") + " " + motionGlyph("scroll") + " Scroll");
        // push, tilt and twist are marked differently
        assert(motionMarker("v_push") != motionMarker("v_tilt") && motionMarker("v_tilt") != motionMarker("twist") && motionMarker("lift") == motionMarker("h_push"));
        // keys are shown, not just the word: timeline has Left / Right on both the horizontal channel and the twist
        const std::vector<MotionRow> tl = motionSummary(*builtins.find("timeline"), id);
        assert(tl[2].detail == "Left / Right" && motionChipTexts(tl)[2] == motionMarker("h_push") + " " + motionGlyph("keys") + " Left / Right");
        assert(motionChipTexts(tl)[2] != motionChipTexts(tl)[5] && motionChipTexts(tl)[2] != motionChipTexts(tl)[3]);
        assert(motionSummaryText(n).find("Lift / press: Not used") != std::string::npos);
        // the glyphs differ per symbol, so the chips are told apart without colour
        assert(motionGlyph("off") != motionGlyph("unused") && motionGlyph("zoom") != motionGlyph("scroll"));
    }

    // a step action
    {
        ModeDef m = *builtins.find("normal");
        m.v.type = ChannelActionType::Step;
        m.v.neg = Key::Left;
        m.v.pos = Key::Right;
        const std::vector<MotionRow> r = motionSummary(m, id);
        assert(r[0].action == "One tap per push: Left / Right" && r[0].chip == "Taps" && r[0].symbol == "step");
    }

    // split channels: push and tilt have their own action; a part without one follows the channel
    {
        ModeDef m = *builtins.find("normal");
        m.vSplit.pushSet = true;
        m.vSplit.push.type = ChannelActionType::WheelV;
        m.vSplit.tiltSet = true;
        m.vSplit.tilt.type = ChannelActionType::Off;
        m.hSplit.tiltSet = true;
        m.hSplit.tilt.type = ChannelActionType::Zoom;
        const std::vector<std::string> c = chips(m);
        assert(c[0] == "Scroll" && c[1] == "Off");
        assert(c[2] == "Sideways" && c[3] == "Zoom");  // H push follows the channel (default = scroll sideways)
        assert(c[5] == "Zoom" && c[4] == "Unused");
        // an explicit "default" on a part is the channel's normal behaviour, not the channel's action
        ModeDef d = *builtins.find("timeline");
        d.hSplit.pushSet = true;
        d.hSplit.push.type = ChannelActionType::Default;
        assert(chips(d)[2] == "Sideways" && chips(d)[3] == "Keys");
    }

    // a mix override: a channel without tilt terms leaves the tilt motion unused, y gives lift an action
    {
        ModeDef m = *builtins.find("normal");
        m.overrides.push_back({"SCROLL_V_MIX", "z"});
        std::vector<MotionRow> r = motionSummary(m, id);
        assert(r[0].kind == MotionKind::Active && r[1].kind == MotionKind::NotUsed && r[1].action == "Not used");
        m.overrides.push_back({"SCROLL_H_MIX", "x - rz + y"});
        r = motionSummary(m, id);
        assert(r[4].kind == MotionKind::Active && r[4].chip == "Sideways" && r[3].chip == "Sideways");
        // the same through the settings argument (a profile mix)
        Settings st;
        st.scroll_v.mix.terms = {{AX_Y, 1.0}};
        r = motionSummary(*builtins.find("normal"), st, id);
        assert(r[0].kind == MotionKind::NotUsed && r[1].kind == MotionKind::NotUsed && r[4].chip == "Scroll");
        // a split channel drops a twist term: the twist belongs to the twist channel
        ModeDef s2 = *builtins.find("normal");
        s2.overrides.push_back({"SCROLL_V_MIX", "z + ry"});
        s2.vSplit.pushSet = true;
        s2.vSplit.push.type = ChannelActionType::Off;
        assert(motionSummary(s2, id)[5].chip == "Zoom");
    }

    // button chips: number and action in a few words; unassigned is muted and says so
    {
        ModeDef m = *builtins.find("normal");
        m.buttons.push_back({1, ButtonActionType::Click, Key::Space, "", {}});
        m.buttons.push_back({2, ButtonActionType::Unbound, Key::Space, "", {}});
        m.buttons.push_back({3, ButtonActionType::ModeSwitch, Key::Space, "timeline", {}});
        m.buttons.push_back({15, ButtonActionType::Nav, Key::Space, "", {}});
        m.buttons.push_back({4, ButtonActionType::Key, Key::Space, "", {}});
        const std::vector<ButtonChip> b = modeButtonChips(m, id, "from the joystick mode");
        assert(b.size() == 4 && b[0].text == "1 Click" && b[1].text == "3 Switch to timeline" && b[2].text == "15 Navigation" &&
               b[3].text == "4 Space");
        assert(!b[0].muted && b[0].tooltip == "Button 1: Click (click), from the joystick mode");
        assert(modeButtonChips(*builtins.find("normal"), id).empty());
        assert(buttonChipsText(b, "; ") == "1 Click; 3 Switch to timeline; 15 Navigation; 4 Space");
        const ButtonChip u = buttonChip(7, "none", "inherited", id);
        assert(u.muted && u.text == "7 Unassigned" && u.source == "inherited");
        assert(buttonActionWords("rightclick", id) == "Right click" && buttonActionWords("middleclick", id) == "Middle click");
        assert(buttonActionWords("key:ctrl+pageup", id) == "Ctrl+Page up" && buttonActionWords("mode:next", id) == "Next joystick mode");
        assert(buttonActionWords("mode:prev:a,b", id) == "Previous joystick mode" && buttonActionWords("toggle:nav", id) == "Toggle navigation");
        assert(buttonActionWords("hold:mode:precision", id) == "Hold: precision" && buttonActionWords("toggle:mode:x", id) == "Toggle: x");
        assert(buttonActionWords("buttonmode:latch", id) == "Button mode: latch" && buttonActionWords("garbage", id) == "Unassigned");
    }

    // translation: every text goes through the provider
    {
        Marking up;
        const std::vector<MotionRow> r = motionSummary(*builtins.find("timeline"), up);
        assert(r[0].name == "<Vertical push>" && r[0].how == "<Push forward / back>" && r[0].action == "<Off>" && r[0].chip == "<Off>");
        assert(r[4].action == "<Not used>" && r[4].chip == "<Unused>");
        assert(r[2].action.rfind("<Keys: ", 0) == 0);
    }
    std::puts("ok");
    return 0;
}
