// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Which raw axis is which motion, per platform and input path (issue #186, toolkit-free).
//
// This table is only the FALLBACK for places that have no configuration to read the roles from (the axis
// assistant before it has measured anything). Where a configuration is shown, axis_roles_model.hpp derives the
// roles from it and wins. Measured with the axis assistant (SpaceMouse Pro):
//   Linux (spacenavd, hid):   push fwd/back z, tilt fwd/back rx, push sideways x, tilt sideways rz, twist ry, lift y
//   Windows, official driver: push fwd/back y, tilt fwd/back rx, push sideways x, tilt sideways ry, twist rz,
//                             lift z (not measured, the one axis left over)
// macOS driver and the direct HID path of Windows and macOS are unmeasured: they use the Linux order.
// TODO(#186): measure macOS (driver) and the HID path on Windows/macOS on hardware; a new order is one enum
// value, one row of kOrders and one case in conventionFor().
#pragma once
#include <array>
#include <string>

#include "common.hpp"

namespace spn {

enum class AxisConvention { Linux, WindowsDriver };

// Rows in the order of ui::Motion: v push, v tilt, h push, h tilt, twist, lift.
inline Axis conventionAxis(AxisConvention c, int motion) {
    static const std::array<std::array<Axis, 6>, 2> kOrders = {{
        {{AX_Z, AX_RX, AX_X, AX_RZ, AX_RY, AX_Y}},  // Linux
        {{AX_Y, AX_RX, AX_X, AX_RY, AX_RZ, AX_Z}},  // WindowsDriver
    }};
    return kOrders[c == AxisConvention::WindowsDriver ? 1 : 0][size_t(motion)];
}
inline Axis conventionLiftAxis(AxisConvention c) { return conventionAxis(c, 5); }
// The motion (0..5) of an axis, the reverse of conventionAxis; -1 if none.
inline int conventionMotion(AxisConvention c, Axis a) {
    for (int m = 0; m < 6; ++m)
        if (conventionAxis(c, m) == a) return m;
    return -1;
}

// os: seedOsName() ("linux", "windows", "macos"); input: the daemon status "driver", "hid" or "" (not reported).
inline AxisConvention conventionFor(const std::string &os, const std::string &input) {
    if (os == "windows" && input != "hid") return AxisConvention::WindowsDriver;
    return AxisConvention::Linux;  // Linux, macOS (unmeasured), direct HID (unmeasured)
}

}  // namespace spn
