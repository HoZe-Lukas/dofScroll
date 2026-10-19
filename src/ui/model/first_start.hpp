// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First start of the interface, without any toolkit: move a legacy configuration over and seed
// the example configuration and modes into the configuration directory (the daemon cannot write
// there). Never overwrites an existing file. The GTK tray did this at its start; the Qt interface
// does it the same way, so an account that starts with the Qt interface does not begin empty.
#pragma once
#include <string>

#include "config.hpp"
#include "seed.hpp"

namespace spn::ui {

struct FirstStart {
    bool migrated = false;  // a legacy configuration was moved over
    std::string migrateLog;
    bool seeded = false;    // example configuration and/or modes were created
    std::string seedLog;

    // Log lines for stderr, each ending in a newline. "who" is the program name.
    std::string text(const std::string &who) const {
        std::string t = who + ": config migration: " + (migrated ? "done" : "not needed or not possible");
        if (!migrateLog.empty()) t += " - " + migrateLog;
        t += "\n";
        if (seeded) t += who + ": seeded defaults: " + seedLog + "\n";
        return t;
    }
};

inline FirstStart runFirstStart() {
    FirstStart r;
    r.migrated = migrateLegacyConfig(&r.migrateLog);
    r.seeded = seedDefaults(&r.seedLog);
    return r;
}

}  // namespace spn::ui
