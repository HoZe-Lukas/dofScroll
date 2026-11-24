// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first device profile of a new installation (ui/model/first_device.hpp) and its progress flag.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <filesystem>
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
    {   // the wired SpaceMouse Pro starts with the lower scroll and zoom sensitivity; every other model follows [global]
        auto kvOf = [](const SettingsModel &m, const std::string &key) {
            for (const KV &kv : m.profiles().sections.back().kv)
                if (kv.first == key) return kv.second;
            return std::string("(none)");
        };
        SettingsModel pro;
        pro.setProfiles(ProfileSet(), "unused.conf");
        assert(addFirstDeviceProfile(pro, "SpaceMouse Pro", &err));
        assert(kvOf(pro, "BUTTON_1") == "nav" && kvOf(pro, "SCROLL_V_SENS") == "3" && kvOf(pro, "SCROLL_V_CURVE") == "2");
        assert(kvOf(pro, "SCROLL_H_SENS") == "2" && kvOf(pro, "SCROLL_H_CURVE") == "2" && kvOf(pro, "ZOOM_SENS") == "1");
        SettingsModel compact;
        compact.setProfiles(ProfileSet(), "unused.conf");
        assert(addFirstDeviceProfile(compact, "3Dconnexion SpaceMouse Compact", &err));
        assert(kvOf(compact, "BUTTON_1") == "nav" && kvOf(compact, "SCROLL_V_SENS") == "(none)" && kvOf(compact, "ZOOM_SENS") == "(none)");
        assert(firstDeviceTuning("3Dconnexion SpaceMouse Pro").size() == 5 && firstDeviceTuning("SPACEMOUSE PRO").size() == 5);
        assert(firstDeviceTuning("SpaceMouse Pro Wireless").empty() && firstDeviceTuning("SpaceMouse Enterprise").empty() && firstDeviceTuning("").empty());
    }
    {   // the split layout writes the same keys into devices/<name>.conf
        namespace stdfs = std::filesystem;
        const stdfs::path root = stdfs::temp_directory_path() /
                                 ("dofscroll-first-device-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        stdfs::create_directories(root, ec);
        assert(!ec);
        const std::string dir = root.string();
        assert(writeFirstDeviceProfileFile(dir, "SpaceMouse Pro", &err));
        {
            std::string found;
            ProfileSet back;
            std::vector<FileDiag> diag;
            assert(loadProfileTree(dir, &back, &diag));
            for (const Section &sec : back.sections)
                if (sec.kind == Section::DEVICE) {
                    assert(sec.pattern == "SpaceMouse Pro");
                    for (const KV &kv : sec.kv) found += kv.first + "=" + kv.second + ";";
                }
            assert(found.find("BUTTON_1=nav;") != std::string::npos && found.find("SCROLL_V_SENS=3;") != std::string::npos &&
                   found.find("ZOOM_SENS=1;") != std::string::npos && found.find("SCROLL_H_CURVE=2;") != std::string::npos);
        }
        stdfs::remove_all(root, ec);
    }
    {   // the flag survives a round trip and is off by default
        UiProgress p;
        assert(!p.firstDevicePending && !parseUiProgress("").firstDevicePending);
        p.firstDevicePending = true;
        assert(parseUiProgress(serializeUiProgress(p)).firstDevicePending);
    }
    return 0;
}
