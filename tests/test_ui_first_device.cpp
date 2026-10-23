// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first device profile of a new installation (ui/model/first_device.hpp) and its progress flag.
#undef NDEBUG
#include <cassert>
#include <string>

#include "ui/model/first_device.hpp"
#include "ui/model/ui_progress.hpp"

using namespace spn;
using namespace spn::ui;

static size_t deviceSections(const SettingsModel &m) {
    size_t n = 0;
    for (const SettingsModel::SectionInfo &s : m.sections())
        if (s.kind == Section::DEVICE) ++n;
    return n;
}

int main() {
    std::string err;
    {   // no device attached yet: nothing happens
        SettingsModel m;
        m.setProfiles(ProfileSet(), "unused.conf");
        assert(!addFirstDeviceProfile(m, "", &err) && deviceSections(m) == 0);
    }
    {   // the attached device gets its profile, button 1 is navigation
        SettingsModel m;
        m.setProfiles(ProfileSet(), "unused.conf");
        assert(addFirstDeviceProfile(m, "SpaceMouse Pro", &err));
        assert(deviceSections(m) == 1);
        assert(m.sections().back().pattern == "SpaceMouse Pro");
        // a second call (or an existing device profile) adds nothing
        assert(!addFirstDeviceProfile(m, "SpaceMouse Compact", &err) && deviceSections(m) == 1);
    }
    {   // the flag survives a round trip and is off by default
        UiProgress p;
        assert(!p.firstDevicePending && !parseUiProgress("").firstDevicePending);
        p.firstDevicePending = true;
        assert(parseUiProgress(serializeUiProgress(p)).firstDevicePending);
    }
    return 0;
}
