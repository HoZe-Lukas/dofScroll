// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Device" page, device info tab: the connected devices, their button count (the number of
// BUTTON_<n> rows a profile offers: factoryButtonCount, never the reported count alone) and
// whether several devices are connected. Toolkit-free; the view only draws the lines.
#pragma once
#include <string>
#include <vector>

#include "device_db.hpp"
#include "platform/platform.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct DeviceInfoLine {
    std::string name;
    int buttons = 0;          // factoryButtonCount(name, reported); 0 = unknown
    bool fromTable = false;   // the count comes from the model table, not from the driver
    int reported = 0;         // what the source reports
    int axes = 6;
    unsigned vendor = 0, product = 0;
};

inline std::vector<DeviceInfoLine> buildDeviceInfo(const std::vector<DeviceInfo> &devices) {
    std::vector<DeviceInfoLine> out;
    for (const DeviceInfo &d : devices) {
        DeviceInfoLine l;
        l.name = d.name;
        l.reported = d.reportedButtons;
        l.axes = d.axes;
        l.vendor = d.vendor;
        l.product = d.product;
        l.fromTable = factoryButtonCount(d.name, 0) > 0;
        const int n = factoryButtonCount(d.name, d.reportedButtons);
        l.buttons = n > 0 ? n : 0;
        out.push_back(l);
    }
    return out;
}

// "SpaceMouse Pro: 15 buttons, 6 axes" (the button count is left out when it is not known).
inline std::string deviceInfoText(const DeviceInfoLine &l, const StringProvider &s) {
    std::string t = l.name.empty() ? s.tr("Unnamed device") : l.name;
    t += ": ";
    t += l.buttons > 0 ? format(s.tr("%1 buttons"), {std::to_string(l.buttons)}) : s.tr("number of buttons unknown");
    t += ", " + format(s.tr("%1 axes"), {std::to_string(l.axes)});
    return t;
}

// Where the button count comes from, for the line below the device.
inline std::string deviceButtonSource(const DeviceInfoLine &l, const StringProvider &s) {
    if (l.buttons <= 0) return s.tr("Up to 32 buttons can be configured on the Profiles page.");
    return l.fromTable ? s.tr("The profile editor offers this many button rows (from the table of known models).")
                       : s.tr("The profile editor offers this many button rows (as reported by the driver).");
}

}  // namespace spn::ui
