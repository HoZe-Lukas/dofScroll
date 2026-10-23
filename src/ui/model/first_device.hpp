// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first device profile of a new installation: the first time the daemon reports an attached
// device, the profile [device:<its name>] is added (button 1 = navigation, everything else follows
// [global]). The seeded configuration has no device profile on purpose: it is the device that is
// really attached, not a model the file guessed. Once only (ui-progress.conf first_device_pending).
// No toolkit.
//
//   if (progress.firstDevicePending && addFirstDeviceProfile(settings, status.device, &err)) save...
#pragma once
#include <string>

#include "ui/model/device_source_model.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

// True if a profile was added (the caller saves the model). False: no device known yet, the
// configuration already has a device profile, or the name is not usable.
inline bool addFirstDeviceProfile(SettingsModel &m, const std::string &statusDevice, std::string *err) {
    DeviceSourceModel src;
    src.setStatusDevice(statusDevice);
    const std::vector<DeviceInfo> devs = src.devices();
    if (devs.empty() || devs.front().name.empty()) return false;
    for (const SettingsModel::SectionInfo &s : m.sections())
        if (s.kind == Section::DEVICE) return false;
    const size_t idx = m.addSection(Section::DEVICE, devs.front().name, err);
    if (idx == ProfileSet::npos) return false;
    return m.setButton(idx, 1, "nav", err);
}

}  // namespace spn::ui
