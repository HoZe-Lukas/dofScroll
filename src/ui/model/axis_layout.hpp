// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Which raw axis name belongs to which motion of the cap on the running system (issue #186, toolkit-free).
// The axis names come from the driver, not from the device: spacenavd (Linux, and the macOS starting point)
// names them z = push forward/back, rz = tilt sideways, ry = twist, y = lift/press; the official 3Dconnexion
// driver on Windows names them y = push forward/back, ry = tilt sideways, rz = twist, z = lift/press (the
// default of examples/default/windows). This is the ONE place that knows that; the motion editor, the motion
// summary and help, the Channels page warning and the axis hint ask it instead of assuming the Linux names.
// The config format is unchanged: files still hold axis names, only the meaning shown to the user follows
// the running system.
//
//   layoutFor(os)            // the layout of one system (tests)
//   currentAxisLayout()      // the layout of this build, or the one set by ScopedAxisLayout (tests)
//   builtinMixText(l, ch)    // "z + rx", "x - rz", "ry": the shipped mix of a channel in that layout
//   builtinSettings(l)       // Settings() with those mixes and ZOOM_LATERAL (the built-in channels)
#pragma once
#include <string>

#include "common.hpp"
#include "config.hpp"
#include "ui/model/axes_model.hpp"

namespace spn::ui {

struct AxisLayout {
    Axis vPush, vTilt;  // vertical scroll: push forward/back, tilt forward/back
    Axis hPush, hTilt;  // horizontal scroll: push sideways, tilt sideways
    Axis twist;         // twisting the cap
    Axis lift;          // pulling up / pressing down
    bool twistNegated;  // the shipped zoom mix is "-<twist>"
};

inline AxisLayout layoutFor(HostOs os) {
    if (os == HostOs::Windows) return {AX_Y, AX_RX, AX_X, AX_RY, AX_RZ, AX_Z, true};
    return {AX_Z, AX_RX, AX_X, AX_RZ, AX_RY, AX_Y, false};  // Linux (spacenavd); macOS: the same until verified
}

namespace axis_layout_detail {
inline const AxisLayout *&override() {
    static const AxisLayout *p = nullptr;
    return p;
}
}  // namespace axis_layout_detail

inline AxisLayout currentAxisLayout() {
    if (const AxisLayout *o = axis_layout_detail::override()) return *o;
    return layoutFor(currentHostOs());
}

// Makes currentAxisLayout() return `l` while it lives (tests of the other systems).
class ScopedAxisLayout {
public:
    explicit ScopedAxisLayout(const AxisLayout &l) : l_(l), prev_(axis_layout_detail::override()) { axis_layout_detail::override() = &l_; }
    ~ScopedAxisLayout() { axis_layout_detail::override() = prev_; }
    ScopedAxisLayout(const ScopedAxisLayout &) = delete;
    ScopedAxisLayout &operator=(const ScopedAxisLayout &) = delete;

private:
    AxisLayout l_;
    const AxisLayout *prev_;
};

enum class LayoutChannel { V, H, Twist };

inline std::string builtinMixText(const AxisLayout &l, LayoutChannel c) {
    switch (c) {
        case LayoutChannel::V: return std::string(axisName(l.vPush)) + " + " + axisName(l.vTilt);
        case LayoutChannel::H: return std::string(axisName(l.hPush)) + " - " + axisName(l.hTilt);
        case LayoutChannel::Twist: return std::string(l.twistNegated ? "-" : "") + axisName(l.twist);
    }
    return "";
}

inline Settings builtinSettings(const AxisLayout &l) {
    Settings s;
    std::string err;
    parseMix(builtinMixText(l, LayoutChannel::V), &s.scroll_v.mix, &err);
    parseMix(builtinMixText(l, LayoutChannel::H), &s.scroll_h.mix, &err);
    parseMix(builtinMixText(l, LayoutChannel::Twist), &s.zoom.mix, &err);
    s.zoom_lateral = {l.hPush, l.vPush, l.vTilt, l.hTilt};
    return s;
}
inline Settings builtinSettings() { return builtinSettings(currentAxisLayout()); }

}  // namespace spn::ui
