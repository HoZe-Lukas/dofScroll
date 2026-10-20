// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live view of the push and tilt parts of a split scroll channel (Axes page, issue #100), toolkit-free.
// The daemon's feed carries the raw axes and the name of the active joystick mode; from these and the
// channel mix of the configuration the page shows, for every channel the active mode splits, the value of
// the push part (translation terms x, y, z of the mix) and of the tilt part (rotation terms rx, rz), both
// before sensitivity and curve, in the units of the axis bars, with what the part does in words.
//
//   splitPartRows(mode, settings, axes, strings)   // empty if the mode splits no channel
#pragma once
#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "modes.hpp"
#include "ui/model/mode_info_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

struct SplitPartRow {
    std::string id;      // "v_push", "v_tilt", "h_push", "h_tilt"
    std::string label;   // "Scroll V push", ...
    std::string action;  // what the part does ("scrolls vertically")
    int value = 0;       // sum of weight * axis over the part's terms
};

inline int mixValue(const Mix &m, const RawAxes &axes) {
    double v = 0;
    for (const Term &t : m.terms) v += t.weight * double(axes[t.axis]);
    return int(v < 0 ? v - 0.5 : v + 0.5);
}

// `settings`: the configuration to take the channel mixes from (a mix override of the mode wins).
inline std::vector<SplitPartRow> splitPartRows(const ModeDef *mode, Settings settings, const RawAxes &axes,
                                               const StringProvider &s) {
    std::vector<SplitPartRow> out;
    if (!mode || (!mode->vSplit.active() && !mode->hSplit.active())) return out;
    for (const auto &kv : mode->overrides) applyKey(settings, utf8::upperAscii(kv.first), kv.second, nullptr);
    struct Chan {
        const char *id;
        const char *labelPush, *labelTilt;
        const Mix *mix;
        const ChannelAction *parent;
        const SplitChannel *split;
    };
    // the labels are literal so that the translation extraction finds them
    const Chan chans[2] = {
        {"v", "Scroll V push", "Scroll V tilt", &settings.scroll_v.mix, &mode->v, &mode->vSplit},
        {"h", "Scroll H push", "Scroll H tilt", &settings.scroll_h.mix, &mode->h, &mode->hSplit}};
    for (const Chan &c : chans) {
        if (!c.split->active()) continue;
        Mix push, tilt;
        splitMix(*c.mix, &push, &tilt);
        out.push_back({std::string(c.id) + "_push", s.tr(c.labelPush), channelActionWords(c.split->pushAction(*c.parent), s),
                       mixValue(push, axes)});
        out.push_back({std::string(c.id) + "_tilt", s.tr(c.labelTilt), channelActionWords(c.split->tiltAction(*c.parent), s),
                       mixValue(tilt, axes)});
    }
    return out;
}

}  // namespace spn::ui
