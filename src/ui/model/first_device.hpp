// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first device profile of a new installation: the first time the daemon reports an attached
// device, the profile [device:<its name>] is added (button 1 = navigation, everything else follows
// [global], except the tuning of a model the maintainer measured, see firstDeviceTuning). The seeded configuration has no device profile on purpose: it is the device that is
// really attached, not a model the file guessed. Once only (ui-progress.conf first_device_pending).
// No toolkit.
//
//   if (progress.firstDevicePending && addFirstDeviceProfile(settings, status.device, &err)) save...
#pragma once
#include <string>
#include <vector>

#include "profile_files.hpp"
#include "ui/model/device_source_model.hpp"
#include "ui/model/settings_model.hpp"
#include "utf8.hpp"

namespace spn::ui {

// The tuning a known model starts with in its first profile, on top of button 1 = navigation: the maintainer found the global
// defaults too lively on the wired SpaceMouse Pro, so its profile starts with a lower scroll sensitivity, a steeper curve (less
// effect at a light touch) and a lower zoom sensitivity. Empty for every other model (they follow [global]). Matched on the
// whole name, so the SpaceMouse Pro Wireless, which was not measured, is not included. Only the first profile of a new
// installation gets it; an existing profile is never changed.
inline std::vector<KV> firstDeviceTuning(const std::string &deviceName) {
    const std::string n = utf8::lowerAscii(utf8::trim(deviceName));
    if (n == "spacemouse pro" || n == "3dconnexion spacemouse pro")
        return {{"SCROLL_V_SENS", "3"}, {"SCROLL_V_CURVE", "2"}, {"SCROLL_H_SENS", "2"}, {"SCROLL_H_CURVE", "2"}, {"ZOOM_SENS", "1"}};
    return {};
}

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
    if (!m.setButton(idx, 1, "nav", err)) return false;
    for (const KV &kv : firstDeviceTuning(devs.front().name))
        if (!m.setValue(idx, kv.first, kv.second, err)) return false;
    return true;
}

// The same for the split layout (docs/plan-config-split.md): the profile is written as devices/<slug>.conf.
// True if the file was written. False: no device known, any device profile exists already (file or legacy
// section), the configuration cannot be read, or the write failed (*err). Nothing else is touched.
inline bool writeFirstDeviceProfileFile(const std::string &configDir, const std::string &statusDevice, std::string *err) {
    DeviceSourceModel src;
    src.setStatusDevice(statusDevice);
    const std::vector<DeviceInfo> devs = src.devices();
    if (devs.empty() || devs.front().name.empty()) return false;
    ProfileSet ps;
    std::vector<FileDiag> diag;
    if (!loadProfileTree(configDir, &ps, &diag)) return false;
    for (const Section &s : ps.sections)
        if (s.kind == Section::DEVICE) return false;
    for (const FileDiag &d : diag)  // a defective device file is not in `ps` but is still somebody's profile
        if (d.origin == FileDiag::PROFILE_FILE && d.kind == Section::DEVICE) return false;
    ProfileSet one;
    const size_t idx = one.findOrAdd(Section::DEVICE, devs.front().name);
    if (idx == ProfileSet::npos) return false;
    one.sections[idx].kv.emplace_back("BUTTON_1", "nav");
    for (const KV &kv : firstDeviceTuning(devs.front().name)) one.sections[idx].kv.push_back(kv);
    const WriteResult w = saveProfileFile(configDir, one.sections[idx], nullptr);
    if (!w.ok() && err) *err = w.error;
    return w.ok();
}

}  // namespace spn::ui
