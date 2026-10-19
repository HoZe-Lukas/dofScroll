// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free source of "which devices exist" for the Profiles page: the device info frame of the
// live feed when the feed is available, otherwise the `device=` line of the status file (one
// device, no vendor:product, no button count). Also decides which device model limits the
// BUTTON_<n> rows of a profile.
//
//   DeviceSourceModel m;
//   m.setFeed(feedModel.hasDevices(), feedModel.devices());
//   m.setStatusDevice(status.device);
//   m.devices();                          // feeds the "Add device..." dialog
//   m.buttonLimitDevice(isDeviceSection, pattern)  // -> {name, reported} for SettingsModel::setDeviceInfo
#pragma once
#include <string>
#include <vector>

#include "live_feed/live_feed_protocol.hpp"
#include "platform/platform.hpp"
#include "profile_match.hpp"

namespace spn::ui {

class DeviceSourceModel {
public:
    // available = a device info frame has been received (an empty list then means: no device).
    void setFeed(bool available, const std::vector<LiveDevice> &devs) {
        feedAvailable_ = available;
        feed_ = available ? devs : std::vector<LiveDevice>();
    }
    // Raw `device=` value of the status file (untrusted text: sanitized and bounded here).
    void setStatusDevice(const std::string &name) { status_ = live_detail::sanitizeName(name); }

    bool fromFeed() const { return feedAvailable_; }

    std::vector<DeviceInfo> devices() const {
        std::vector<DeviceInfo> out;
        if (feedAvailable_) {
            for (const LiveDevice &d : feed_) {
                DeviceInfo di;
                di.id = d.id;
                di.name = d.name;
                di.vendor = d.vendor;
                di.product = d.product;
                di.reportedButtons = d.reportedButtons;
                di.axes = d.axes;
                out.push_back(std::move(di));
            }
        } else if (!status_.empty()) {
            DeviceInfo di;
            di.name = status_;
            out.push_back(std::move(di));
        }
        return out;
    }

    struct Limit {
        std::string name;
        int reported = 0;  // 0 = unknown: SettingsModel then uses the model table or the maximum
    };
    // Device whose model limits the button rows: for a device profile the connected device the
    // pattern matches (its reported count refines unknown models), else the pattern itself; for all
    // other profiles the first connected device.
    Limit buttonLimitDevice(bool deviceSection, const std::string &pattern) const {
        const std::vector<DeviceInfo> devs = devices();
        Limit l;
        if (deviceSection) {
            l.name = pattern;
            for (const DeviceInfo &d : devs)
                if (patternMatches(pattern, d.name)) {
                    l.name = d.name;
                    l.reported = d.reportedButtons;
                    break;
                }
        } else if (!devs.empty()) {
            l.name = devs.front().name;
            l.reported = devs.front().reportedButtons;
        }
        return l;
    }

private:
    bool feedAvailable_ = false;
    std::vector<LiveDevice> feed_;
    std::string status_;
};

}  // namespace spn::ui
