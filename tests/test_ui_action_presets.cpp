// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The action tiles of the visual joystick mode editor (issue #143): the list, what each tile writes and which
// tile is lit for an action.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <set>

#include "ui/model/action_preset_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct English : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
}  // namespace

int main() {
    English s;
    const std::vector<ActionPreset> all = actionPresets(s);
    assert(all.size() == 11);
    std::set<std::string> ids;
    for (const ActionPreset &p : all) {
        assert(!p.id.empty() && !p.label.empty() && !p.glyph.empty() && !p.tip.empty() && ids.insert(p.id).second);
    }
    // every tile writes a valid action, and the tile that is lit after writing it is the same tile
    for (Motion m : allMotions()) {
        if (m == Motion::Lift) continue;
        for (const ActionPreset &p : all) {
            const ChannelForm f = presetForm(p.id, m, ChannelForm());
            ChannelAction a;
            std::string err;
            assert(parseChannelAction(f.text(), &a, &err));
            // "Other keys" starts as up / down: the same keys as the arrow tile on a vertical motion, which is the one lit
            assert(activePreset(m, f) == p.id || (p.id == "keys" && activePreset(m, f) == "arrows"));
        }
    }
    // the arrow keys follow the motion
    assert(presetForm("arrows", Motion::VPush, ChannelForm()).text() == "keys:up,down");
    assert(presetForm("arrows", Motion::VTilt, ChannelForm()).text() == "keys:up,down");
    assert(presetForm("arrows", Motion::HPush, ChannelForm()).text() == "keys:left,right");
    assert(presetForm("arrows", Motion::Twist, ChannelForm()).text() == "keys:left,right");
    assert(presetForm("pages", Motion::HTilt, ChannelForm()).text() == "keys:pageup,pagedown");
    // "Other keys" and "taps" keep the keys the motion has
    ChannelForm cur;
    cur.type = ChannelActionType::Keys;
    cur.neg = "ctrl+left";
    cur.pos = "ctrl+right";
    assert(presetForm("taps", Motion::HPush, cur).text() == "step:ctrl+left,ctrl+right");
    assert(presetForm("keys", Motion::HPush, ChannelForm()).text() == "keys:up,down");
    assert(activePreset(Motion::HPush, cur) == "keys");  // any other keys: the "Other keys" tile
    // the arrow keys of the other direction are other keys on a vertical motion
    ChannelForm lr;
    lr.type = ChannelActionType::Keys;
    lr.neg = "left";
    lr.pos = "right";
    assert(activePreset(Motion::VPush, lr) == "keys" && activePreset(Motion::HPush, lr) == "arrows");
    std::puts("test_ui_action_presets OK");
    return 0;
}
