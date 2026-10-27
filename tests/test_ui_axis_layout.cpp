// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The axis names the interface shows follow the running system (issue #186): the Linux names (z = push,
// rz = tilt sideways, ry = twist, y = lift) and the Windows driver names (y = push, ry = tilt sideways,
// rz = twist, z = lift) from one place, src/ui/model/axis_layout.hpp.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/axis_layout.hpp"
#include "ui/model/channels_page_model.hpp"
#include "ui/model/motion_help_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/motion_summary_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
bool has(const std::string &t, const std::string &p) { return t.find(p) != std::string::npos; }
}  // namespace

static void testLayouts() {
    const AxisLayout lin = layoutFor(HostOs::Linux), win = layoutFor(HostOs::Windows);
    assert(builtinMixText(lin, LayoutChannel::V) == "z + rx" && builtinMixText(lin, LayoutChannel::H) == "x - rz" &&
           builtinMixText(lin, LayoutChannel::Twist) == "ry");
    // the shipped Windows default: SCROLL_V_MIX = y + rx, SCROLL_H_MIX = x - ry, ZOOM_MIX = -rz
    assert(builtinMixText(win, LayoutChannel::V) == "y + rx" && builtinMixText(win, LayoutChannel::H) == "x - ry" &&
           builtinMixText(win, LayoutChannel::Twist) == "-rz");
    assert(win.lift == AX_Z && lin.lift == AX_Y);
    // the Linux layout is what Settings() has always held
    const Settings def, b = builtinSettings(lin);
    assert(mixToString(def.scroll_v.mix) == mixToString(b.scroll_v.mix) && mixToString(def.scroll_h.mix) == mixToString(b.scroll_h.mix) &&
           mixToString(def.zoom.mix) == mixToString(b.zoom.mix) && def.zoom_lateral == b.zoom_lateral);
    assert(mixToString(builtinSettings(win).zoom.mix) == "-rz");
}

static void testWarning() {
    {
        ScopedAxisLayout g(layoutFor(HostOs::Linux));
        assert(mixUsesLiftPress("z + y") && !mixUsesLiftPress("z + rx") && !mixUsesLiftPress("-ry"));
    }
    ScopedAxisLayout g(layoutFor(HostOs::Windows));
    // on Windows y is push forward/back and z is lift/press
    assert(!mixUsesLiftPress("y + rx") && !mixUsesLiftPress("x - ry") && !mixUsesLiftPress("-rz"));
    assert(mixUsesLiftPress("y + rx + z"));
}

static void testMotions() {
    IdentityStrings s;
    ScopedAxisLayout g(layoutFor(HostOs::Windows));
    assert(motionAxis(Motion::VPush) == AX_Y && motionAxis(Motion::HTilt) == AX_RY && motionAxis(Motion::Twist) == AX_RZ &&
           motionAxis(Motion::Lift) == AX_Z);
    assert(motionOfAxis(AX_RZ) == Motion::Twist);
    // the Windows default settings use every motion except lift, and nothing counts as a custom mix
    const Settings base = builtinSettings();
    ModeForm f;
    for (Motion m : allMotions()) {
        const MotionPlace p = placeOf(f, base, m);
        assert(p.found == (m != Motion::Lift));
        assert(!p.customMix);
    }
    assert(placeOf(f, base, Motion::Twist).channel == MotionChannel::Twist);
    assert(placeOf(f, base, Motion::HTilt).channel == MotionChannel::H && placeOf(f, base, Motion::HTilt).part);
    // the help names the axes of the system
    assert(has(helpTopic("motion:twist", s).body, "axis rz"));
    assert(has(helpTopic("motion:v_push", s).body, "axis y"));
    assert(has(helpTopic("motion:h_tilt", s).body, "axis ry"));
    assert(has(helpTopic("motion:lift", s).body, "axis z") && has(helpTopic("motion:lift", s).body, "SCROLL_V_MIX = y + rx + z"));
    // the summary: lift unused, every other motion found
    const ModeSet builtins = loadModes("/nonexistent-dir-for-test", nullptr);
    const ModeDef *normal = builtins.find("normal");
    assert(normal);
    for (const MotionRow &r : motionSummary(*normal, base, s)) assert((r.kind == MotionKind::NotUsed) == (r.id == "lift"));
}

int main() {
    testLayouts();
    testWarning();
    testMotions();
    {
        // and Linux still reads as before
        IdentityStrings s;
        ScopedAxisLayout g(layoutFor(HostOs::Linux));
        assert(has(helpTopic("motion:twist", s).body, "axis ry") && has(helpTopic("motion:lift", s).body, "SCROLL_V_MIX = z + rx + y"));
        assert(motionAxis(Motion::VPush) == AX_Z);
    }
    std::puts("axis layout tests passed");
    return 0;
}
