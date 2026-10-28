// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The channel actions a profile run really uses: the joystick mode's (modes.hpp) with the actions that profiles set
// (V_ACTION, H_ACTION, TWIST_ACTION, V_PUSH_ACTION, V_TILT_ACTION, H_PUSH_ACTION, H_TILT_ACTION, config.hpp)
// merged in. One function for the daemon and the interface, so both agree.
//
// Precedence, lowest first: [global] (Settings::actions[kLayerGlobal]), [device:*] (kLayerDevice), the joystick mode,
// [app:*] (kLayerApp); the program has the last word. A key that is not set leaves the layer below as it is. The mode
// counts as setting a channel when its action is not "default" (or it splits the channel), so a mode that says nothing
// about a channel leaves [global] and [device:*] in force, and a mode that sets one beats them.
// A layer that sets a channel's own key (V_ACTION) without a split key makes the whole channel that action: it drops the
// split of every layer below, [global] included when the layer is [device:*]. A split key (V_TILT_ACTION) changes only
// that part; the other part keeps what the layers below (or the channel's action) say, so "tilt off, push as before" is
// one key.
// Lift (LIFT_ACTION) has one key and no split: the highest layer that sets it wins; "default" means none.
#pragma once
#include "config.hpp"
#include "modes.hpp"

namespace spn {

struct ChannelActions {
    ChannelAction v, h, twist, lift;
    SplitChannel vSplit, hSplit;
    bool allDefault() const {
        return v.type == ChannelActionType::Default && h.type == ChannelActionType::Default &&
               twist.type == ChannelActionType::Default && liftIdle() && !vSplit.active() && !hSplit.active();
    }
    // The lift does nothing unless an action says so ("default" and "none" are the same): such a profile stays on the
    // normal path (NAV_BUTTON and CLICK_BUTTON keep working).
    bool liftIdle() const {
        return lift.type == ChannelActionType::Default || lift.type == ChannelActionType::Off;
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
    const ProfileActions &glob = s.actions[kLayerGlobal], &dev = s.actions[kLayerDevice], &app = s.actions[kLayerApp];
    if (!glob.any() && !dev.any() && !app.any()) {  // the common case: exactly the mode
        r.v = m.v;
        r.h = m.h;
        r.twist = m.twist;
        r.lift = m.lift;
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
        for (const ProfileActions *pa : {&glob, &dev})
            layer(act, sp, pa->chanSet[c], pa->chan[c], pa->partSet[pu], pa->part[pu], pa->partSet[ti], pa->part[ti]);
        layer(act, sp, mact.type != ChannelActionType::Default, mact, msp.pushSet, msp.push, msp.tiltSet, msp.tilt);
        layer(act, sp, app.chanSet[c], app.chan[c], app.partSet[pu], app.part[pu], app.partSet[ti], app.part[ti]);
    }
    // Twist and lift have one key each: the highest layer that sets it wins.
    for (int c : {int(ProfileActions::TWIST), int(ProfileActions::LIFT)}) {
        ChannelAction &out = c == ProfileActions::TWIST ? r.twist : r.lift;
        const ChannelAction &mact = c == ProfileActions::TWIST ? m.twist : m.lift;
        for (const ProfileActions *pa : {&glob, &dev})
            if (pa->chanSet[c]) out = pa->chan[c];
        if (mact.type != ChannelActionType::Default) out = mact;
        if (app.chanSet[c]) out = app.chan[c];
    }
    return r;
}

}  // namespace spn
