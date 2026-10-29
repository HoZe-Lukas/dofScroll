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
    assert(all.size() == 14);
    std::set<std::string> ids;
    for (const ActionPreset &p : all) {
        assert(!p.id.empty() && !p.label.empty() && !p.glyph.empty() && !p.tip.empty() && ids.insert(p.id).second);
    }
    // on lift (digital only) every tile but "Other keys" writes an action the lift parser accepts, and the same tile is lit
    for (const ActionPreset &p : all) {
        if (p.id == "keys") continue;
        const ChannelForm f = presetForm(p.id, Motion::Lift, ChannelForm());
        ChannelAction a;
        std::string err;
        assert(f.type != ChannelActionType::Keys && parseLiftAction(f.text(), &a, &err));
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
    // the ready choices are made from the built-in example modes (single source): no tile and example can drift apart
    {
        const ModeSet lib = loadModes("/nonexistent-dir-for-test", nullptr);
        struct Src {
            const char *tile, *mode;
            bool horizontal;  // the channel of the example that carries the action
            bool analogOk;    // the tile writes keys: (tier A may be analog); else step:
        };
        const Src srcs[] = {{"pages", "slides", false, true}, {"tabs", "tabs", true, true}, {"volume", "volume", false, true}, {"tracks", "tracks", true, false}};
        for (const Src &x : srcs) {
            const ModeDef *md = lib.find(x.mode);
            assert(md && md->builtin);
            const ChannelAction &ex = x.horizontal ? md->h : md->v;
            assert(ex.usesKeys());
            assert((ex.type == ChannelActionType::Keys) == x.analogOk);
            for (Motion m : allMotions()) {
                const ChannelForm f = presetForm(x.tile, m, ChannelForm());
                ChannelAction a;
                std::string err;
                if (m == Motion::Lift) {
                    // the lift is digital only: the same keys as step:, never keys:
                    assert(f.type == ChannelActionType::Step && parseLiftAction(f.text(), &a, &err));
                    ChannelAction digital = ex;
                    digital.type = ChannelActionType::Step;
                    assert(a == digital && channelActionText(digital) == f.text());
                    assert(activePreset(m, f) == x.tile);
                } else {
                    assert(parseChannelAction(f.text(), &a, &err) && a == ex && channelActionText(ex) == f.text());
                    assert(activePreset(m, f) == x.tile);
                }
            }
        }
        // the arrow keys tile is what the "map" example does on the vertical and the horizontal channel
        const ModeDef *map = lib.find("map");
        assert(map && channelActionText(map->v) == presetForm("arrows", Motion::VPush, ChannelForm()).text() &&
               channelActionText(map->h) == presetForm("arrows", Motion::HPush, ChannelForm()).text());
        // the keys the tiles write
        assert(presetForm("tabs", Motion::HPush, ChannelForm()).text() == "keys:ctrl+pageup,ctrl+pagedown");
        assert(presetForm("volume", Motion::Twist, ChannelForm()).text() == "keys:volume_up,volume_down");
        assert(presetForm("tracks", Motion::HTilt, ChannelForm()).text() == "step:media_prev,media_next");
        assert(presetForm("tabs", Motion::Lift, ChannelForm()).text() == "step:ctrl+pageup,ctrl+pagedown");
        // a digital version of an analog tile is not that tile on a motion: it is one key tap per push
        ChannelForm tapTabs = presetForm("tabs", Motion::HPush, ChannelForm());
        tapTabs.type = ChannelActionType::Step;
        assert(activePreset(Motion::HPush, tapTabs) == "taps");
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
