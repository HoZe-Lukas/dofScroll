// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The settings of a fresh state (no configuration yet), toolkit-free: the default file of this system that the
// first start seeds, examples/default/<os>/dofscroll.conf (the choice of the file is seedOsName(), the same as
// for the seeding; nothing else in the UI knows the system). Its values are not repeated in code. Without that
// file (not installed, not found) the built-in Settings() of the core is used.
//
//   freshSettings()            // the settings the UI shows when no profile is at hand
//   setFreshSettings(s)        // tests: a fixture instead of the file of the host system
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "seed.hpp"

namespace spn::ui {

namespace fresh_detail {
inline std::unique_ptr<Settings> &slot() {
    static std::unique_ptr<Settings> s;
    return s;
}
}  // namespace fresh_detail

inline Settings settingsOfFile(const std::string &path, bool *ok = nullptr) {
    ProfileSet ps;
    std::vector<std::string> warn;
    const bool good = loadProfiles(path, &ps, &warn);
    if (ok) *ok = good;
    return good ? ps.sectionSettings(0) : Settings();
}

inline void setFreshSettings(const Settings &s) { fresh_detail::slot() = std::make_unique<Settings>(s); }

inline const Settings &freshSettings() {
    std::unique_ptr<Settings> &p = fresh_detail::slot();
    if (!p) {
        const std::string dir = examplesDir();
        bool ok = false;
        Settings s;
        if (!dir.empty()) s = settingsOfFile(dir + "/default/" + seedOsName() + "/dofscroll.conf", &ok);
        p = std::make_unique<Settings>(ok ? s : Settings());
    }
    return *p;
}

}  // namespace spn::ui
