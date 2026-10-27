// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The channel actions a profile run really uses: the joystick mode's (modes.hpp) with the actions that profiles set
// (V_ACTION, H_ACTION, TWIST_ACTION, V_PUSH_ACTION, V_TILT_ACTION, H_PUSH_ACTION, H_TILT_ACTION, config.hpp)
// merged in. One function for the daemon and the interface, so both agree.
//
// Precedence, lowest first: [global] and [device:*] (Settings::actions[0]), the joystick mode, [app:*]
// (Settings::actions[1]); the program has the last word. A key that is not set leaves the layer below as it is. The
// mode counts as setting a channel when its action is not "default" (or it splits the channel), so a mode that says
// nothing about a channel leaves [global] and [device:*] in force, and a mode that sets one beats them.
// A layer that sets a channel's own key (V_ACTION) without a split key makes the whole channel that action: it drops the
// split of the layers below. A split key (V_TILT_ACTION) changes only that part; the other part keeps what the layers below
// (or the channel's action) say, so "tilt off, push as before" is one key.
#pragma once
#include "config.hpp"
#include "modes.hpp"

namespace spn {

struct ChannelActions {
    ChannelAction v, h, twist;
    SplitChannel vSplit, hSplit;
    bool allDefault() const {
        return v.type == ChannelActionType::Default && h.type == ChannelActionType::Default &&
               twist.type == ChannelActionType::Default && !vSplit.active() && !hSplit.active();
    }
};

namespace profile_actions_detail {
// One layer of one scroll channel on top of the current state.
inline void layer(ChannelAction *act, SplitChannel *sp, bool chanSet, const ChannelAction &a, bool pushSet,
                  const ChannelAction &push, bool tiltSet, const ChannelAction &tilt) {
    if (chanSet) {
        *act = a;
        *sp = SplitChannel();
    }
    if (pushSet) {
        sp->push = push;
        sp->pushSet = true;
    }
    if (tiltSet) {
        sp->tilt = tilt;
        sp->tiltSet = true;
    }
}
}  // namespace profile_actions_detail

inline ChannelActions effectiveChannelActions(const ModeDef &m, const Settings &s) {
    using namespace profile_actions_detail;
    ChannelActions r;
    const ProfileActions &base = s.actions[0], &app = s.actions[1];
    if (!base.any() && !app.any()) {  // the common case: exactly the mode
        r.v = m.v;
        r.h = m.h;
        r.twist = m.twist;
        r.vSplit = m.vSplit;
        r.hSplit = m.hSplit;
        return r;
    }
    for (int c = 0; c < 2; ++c) {
        ChannelAction *act = c == 0 ? &r.v : &r.h;
        SplitChannel *sp = c == 0 ? &r.vSplit : &r.hSplit;
        const ChannelAction &mact = c == 0 ? m.v : m.h;
        const SplitChannel &msp = c == 0 ? m.vSplit : m.hSplit;
        const int pu = c * 2, ti = c * 2 + 1;
        layer(act, sp, base.chanSet[c], base.chan[c], base.partSet[pu], base.part[pu], base.partSet[ti], base.part[ti]);
        layer(act, sp, mact.type != ChannelActionType::Default, mact, msp.pushSet, msp.push, msp.tiltSet, msp.tilt);
        layer(act, sp, app.chanSet[c], app.chan[c], app.partSet[pu], app.part[pu], app.partSet[ti], app.part[ti]);
    }
    r.twist = base.chanSet[ProfileActions::TWIST] ? base.chan[ProfileActions::TWIST] : ChannelAction();
    if (m.twist.type != ChannelActionType::Default) r.twist = m.twist;
    if (app.chanSet[ProfileActions::TWIST]) r.twist = app.chan[ProfileActions::TWIST];
    return r;
}

}  // namespace spn
