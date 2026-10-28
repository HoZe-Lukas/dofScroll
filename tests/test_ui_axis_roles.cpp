// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the axes mean comes from the configuration, not from code (issue #186): the roles are derived from the
// effective mixes of what is shown. Table-driven: a new operating system default, device or driver order is one
// row; nothing in src/ui needs a code change for it (the made-up rows prove it).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "axis_convention.hpp"
#include "ui/model/axis_roles_model.hpp"
#include "ui/model/channels_page_model.hpp"
#include "ui/model/fresh_state.hpp"
#include "ui/model/motion_help_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/motion_summary_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
bool has(const std::string &t, const std::string &p) { return t.find(p) != std::string::npos; }

Settings fromMixes(const std::string &v, const std::string &h, const std::string &z) {
    Settings s;
    std::string err;
    assert(applyKey(s, "SCROLL_V_MIX", v, &err) && applyKey(s, "SCROLL_H_MIX", h, &err) && applyKey(s, "ZOOM_MIX", z, &err));
    return s;
}

const char *kNone = "-";
// the expected axis of each motion, "-" = no hint
struct Row {
    const char *what;
    Settings s;
    unsigned mask;
    bool matched;
    const char *vPush, *vTilt, *hPush, *hTilt, *twist, *lift;
};

void check(const Row &r) {
    const AxisRoles a = deriveAxisRoles(r.s, r.mask);
    if (a.matched != r.matched) std::fprintf(stderr, "FAIL %s: matched\n", r.what);
    assert(a.matched == r.matched);
    const char *want[6] = {r.vPush, r.vTilt, r.hPush, r.hTilt, r.twist, r.lift};
    const Motion order[6] = {Motion::VPush, Motion::VTilt, Motion::HPush, Motion::HTilt, Motion::Twist, Motion::Lift};
    for (int i = 0; i < 6; ++i) {
        const std::string got = a.has(order[i]) ? axisName(a.axis(order[i])) : kNone;
        if (got != want[i]) std::fprintf(stderr, "FAIL %s: motion %d is %s, expected %s\n", r.what, i, got.c_str(), want[i]);
        assert(got == want[i]);
    }
}
}  // namespace

static void testTable() {
    const unsigned all = kAllAxes;
    const unsigned noZ = all & ~(1u << AX_Z);
    const std::vector<Row> rows = {
        // the shipped defaults of every system: the files themselves (a change that breaks the derivation fails here)
        {"examples/default/linux", settingsOfFile("examples/default/linux/dofscroll.conf"), all, true, "z", "rx", "x", "rz", "ry", "y"},
        {"examples/default/windows", settingsOfFile("examples/default/windows/dofscroll.conf"), all, true, "y", "rx", "x", "ry", "rz", "z"},
        {"examples/default/macos", settingsOfFile("examples/default/macos/dofscroll.conf"), all, true, "y", "rx", "x", "ry", "rz", "z"},
        {"built-in Settings()", Settings(), all, true, "z", "rx", "x", "rz", "ry", "y"},
        // made up: a driver with another order (nothing in src/ui knows it)
        {"made-up order", fromMixes("x + ry", "z - rx", "rz"), all, true, "x", "ry", "z", "rx", "rz", "y"},
        // made up: inverted axes and weights do not change what an axis is
        {"inverted", fromMixes("-z + 0.5*rx", "-x + rz", "-ry"), all, true, "z", "rx", "x", "rz", "ry", "y"},
        // made up: a device with fewer axes (no z): lift/press is not certain, the rest is
        {"four-axis-like, no z", fromMixes("y + rx", "x - ry", "rz"), noZ, true, "y", "rx", "x", "ry", "rz", kNone},
        // made up: push only, no tilt terms
        {"push only", fromMixes("z", "x", "ry"), all, true, "z", kNone, "x", kNone, "ry", "y"},
        // lift/press typed into a mix stays recognised (an extra translation term is an addition)
        {"lift in the mix", fromMixes("z + rx + y", "x - rz", "ry"), all, true, "z", "rx", "x", "rz", "ry", "y"},
        // does not match: no hint at all, never a wrong name
        {"two pushes, no tilt, in V and a tilt in zoom", fromMixes("z + x", "x - rz", "rx"), all, false, kNone, kNone, kNone, kNone, kNone, kNone},
        {"twist twice", fromMixes("z + rx", "x - rz", "ry + rx"), all, false, kNone, kNone, kNone, kNone, kNone, kNone},
        {"same axis in two channels", fromMixes("z + rx", "z - rz", "ry"), all, false, kNone, kNone, kNone, kNone, kNone, kNone},
    };
    for (const Row &r : rows) check(r);
}

static void testNeutralWords() {
    IdentityStrings s;
    const Settings st = fromMixes("z + x + y", "x - rz", "ry");  // does not match: V has no tilt
    const AxisRoles r = deriveAxisRoles(st);
    assert(!r.matched && !r.has(Motion::Lift));
    assert(axisRoleText(r, AX_Y, s) == "Axis y scrolls vertically");
    assert(axisRoleText(r, AX_X, s) == "Axis x scrolls vertically, scrolls horizontally");
    assert(axisRoleText(r, AX_RY, s) == "Axis ry zooms");
    assert(axisRoleText(r, AX_RX, s) == "Axis rx is in no channel mix");
    const std::string hint = axesHint(r, st, s);
    assert(has(hint, "vertical scroll uses the axes z, x, y") && has(hint, "horizontal scroll x, rz") && has(hint, "zoom ry"));
    // matched: the physical sentence with the names of the configuration
    const Settings win = settingsOfFile("examples/default/windows/dofscroll.conf");
    assert(has(axesHint(deriveAxisRoles(win), win, s), "Push (y, x, z) and tilt (rx, ry) are the two parts") &&
           has(axesHint(deriveAxisRoles(win), win, s), "twist (rz) is zoom"));
}

static void testUserChangesFollow() {
    IdentityStrings s;
    // the user (or the axis assistant) changes a mix on top of the Windows default: the UI follows the file
    Settings st = settingsOfFile("examples/default/windows/dofscroll.conf");
    std::string err;
    assert(applyKey(st, "SCROLL_V_MIX", "z + rx", &err) && applyKey(st, "SCROLL_H_MIX", "x - rz", &err) && applyKey(st, "ZOOM_MIX", "ry", &err));
    AxisRoles r = deriveAxisRoles(st);
    assert(r.axis(Motion::VPush) == AX_Z && r.axis(Motion::Twist) == AX_RY && r.axis(Motion::Lift) == AX_Y);
    assert(has(helpTopic("motion:twist", s, &r).body, "axis ry") && has(helpTopic("motion:lift", s, &r).body, "SCROLL_V_MIX = z + rx + y"));
    // a mode override changes the effective mix: the motion editor reads the effective settings
    st = settingsOfFile("examples/default/windows/dofscroll.conf");
    ModeForm f;
    f.setOverride("SCROLL_V_MIX", "y + rx + z");
    r = deriveAxisRoles(st);  // the roles are those of the profile; the override only moves z into the V channel
    assert(r.matched && r.axis(Motion::Lift) == AX_Z);
    assert(placeOf(f, st, Motion::Lift).found && placeOf(f, st, Motion::Lift).channel == MotionChannel::V);
    // a configuration that matches no pattern: the six motions have no axis, the rows say "not used"
    Settings odd = fromMixes("z + x + y", "x - rz", "ry");
    const ModeSet builtins = loadModes("/nonexistent-dir-for-test", nullptr);
    for (const MotionRow &row : motionSummary(*builtins.find("normal"), odd, s)) assert(row.kind == MotionKind::NotUsed);
    assert(!placeOf(ModeForm(), odd, Motion::VPush).found);
    const AxisRoles oddRoles = deriveAxisRoles(odd);
    assert(has(helpTopic("motion:v_push", s, &oddRoles).body, "assigns no axis"));
}

static void testDeviceProfile() {
    // a device profile with its own mixes: the roles of THAT device's effective settings
    const std::string path = (std::filesystem::temp_directory_path() / "dofscroll_axis_roles_test.conf").string();
    {
        std::ofstream o(path);
        o << "[global]\nSCROLL_V_MIX = z + rx\nSCROLL_H_MIX = x - rz\nZOOM_MIX = ry\n\n"
             "[device:Other Driver Mouse]\nSCROLL_V_MIX = y + rx\nSCROLL_H_MIX = x - ry\nZOOM_MIX = -rz\n";
    }
    ProfileSet ps;
    std::vector<std::string> warn;
    assert(loadProfiles(path, &ps, &warn));
    std::filesystem::remove(path);
    const AxisRoles g = deriveAxisRoles(ps.resolve("", ""));
    const AxisRoles d = deriveAxisRoles(ps.resolve("Other Driver Mouse", ""));
    assert(g.axis(Motion::VPush) == AX_Z && g.axis(Motion::Lift) == AX_Y);
    assert(d.axis(Motion::VPush) == AX_Y && d.axis(Motion::HTilt) == AX_RY && d.axis(Motion::Twist) == AX_RZ && d.axis(Motion::Lift) == AX_Z);
}

static void testLiftWatch() {
    LiftWatch w;
    assert(!w.known() && !w.usesLift("y"));  // nothing sure yet: no warning
    w.observe(settingsOfFile("examples/default/linux/dofscroll.conf"));
    assert(w.known() && w.lift() == AX_Y);
    assert(w.usesLift("y") && w.usesLift("z + 0.5*y") && w.usesLift("-y"));
    assert(!w.usesLift("z + rx") && !w.usesLift("0*y") && !w.usesLift("not a mix"));
    // typing "y" as the whole vertical mix breaks the pattern: the last sure answer stays
    w.observe(fromMixes("y", "x - rz", "ry"));
    assert(w.lift() == AX_Y && w.usesLift("y"));
    // a configuration whose pattern holds again with another lift axis (the Windows order) moves the warning
    w.observe(settingsOfFile("examples/default/windows/dofscroll.conf"));
    assert(w.lift() == AX_Z && w.usesLift("y + z") && !w.usesLift("y + rx"));
    // a pattern that does not hold from the start never warns
    LiftWatch none;
    none.observe(fromMixes("z + x + y", "x - rz", "ry"));
    assert(!none.known() && !none.usesLift("y"));
}

// The words of the Motion help and of the Axes page hint never carry axis names of their own: an unusual
// configuration shows its own names (issue #249).
static void testNamesOnlyFromConfiguration() {
    IdentityStrings s;
    const Settings odd = fromMixes("rx", "ry", "x");
    const AxisRoles r = deriveAxisRoles(odd);
    const std::string hint = axesHint(r, odd, s);
    assert(has(hint, "rx") && has(hint, "ry") && has(hint, "zoom x"));
    assert(!has(hint, "rz") && !has(hint, " y") && !has(hint, "axes z") && !has(hint, "(y"));
    for (const char *id : {"motion:v_push", "motion:v_tilt", "motion:h_push", "motion:h_tilt", "motion:twist", "motion:lift"}) {
        const std::string body = helpTopic(id, s, &r).body;
        assert(!has(body, "axis rz") && !has(body, "axis y") && !has(body, "axis z"));
    }
}

static void testFreshState() {
    // the fresh state is a fixture here, not the file of the host system
    setFreshSettings(settingsOfFile("examples/default/windows/dofscroll.conf"));
    IdentityStrings s;
    assert(has(helpTopic("motion:twist", s).body, "axis rz"));
    assert(motionSummary(*loadModes("/nonexistent-dir-for-test", nullptr).find("normal"), s)[4].kind == MotionKind::NotUsed);
    setFreshSettings(settingsOfFile("examples/default/linux/dofscroll.conf"));
    assert(has(helpTopic("motion:twist", s).body, "axis ry"));
}

static void testSplitMix() {
    // every rotation axis is a tilt part, whatever the driver calls it
    Mix mp, mt;
    Mix h;
    std::string err;
    assert(parseMix("x - ry", &h, &err));
    splitMix(h, &mp, &mt);
    assert(mixToString(mp) == "x" && mixToString(mt) == "-ry");
    assert(parseMix("y + rx", &h, &err));
    splitMix(h, &mp, &mt);
    assert(mixToString(mp) == "y" && mixToString(mt) == "rx");
}

// The per-platform / per-input-path fallback table (axis_convention.hpp) agrees with the seeded defaults of the
// systems that were measured, and the platform is a parameter so every row runs on Linux.
static void testConvention() {
    assert(conventionFor("linux", "") == AxisConvention::Linux);
    assert(conventionFor("linux", "hid") == AxisConvention::Linux);
    assert(conventionFor("windows", "driver") == AxisConvention::WindowsDriver);
    assert(conventionFor("windows", "") == AxisConvention::WindowsDriver);
    assert(conventionFor("windows", "hid") == AxisConvention::Linux);  // unmeasured: Linux order
    assert(conventionFor("macos", "driver") == AxisConvention::Linux);  // unmeasured, not guessed
    assert(conventionFor("macos", "hid") == AxisConvention::Linux);
    const struct {
        const char *os;
        AxisConvention c;
    } measured[] = {{"linux", AxisConvention::Linux}, {"windows", AxisConvention::WindowsDriver}};
    for (const auto &m : measured) {
        const AxisRoles r = deriveAxisRoles(settingsOfFile(std::string("examples/default/") + m.os + "/dofscroll.conf"));
        assert(r.matched && r.complete());
        for (int mo = 0; mo < kMotionCount; ++mo) {
            assert(r.axisOf[size_t(mo)] == int(conventionAxis(m.c, mo)));
            assert(conventionMotion(m.c, conventionAxis(m.c, mo)) == mo);
        }
    }
    assert(conventionLiftAxis(AxisConvention::Linux) == AX_Y && conventionLiftAxis(AxisConvention::WindowsDriver) == AX_Z);
}

int main() {
    testConvention();
    testTable();
    testNeutralWords();
    testUserChangesFollow();
    testDeviceProfile();
    testLiftWatch();
    testNamesOnlyFromConfiguration();
    testFreshState();
    testSplitMix();
    std::puts("axis roles tests passed");
    return 0;
}
