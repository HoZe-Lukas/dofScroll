// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First start of the interface, without any toolkit: move a legacy configuration over and seed
// the example configuration and modes into the configuration directory (the daemon cannot write
// there). Never overwrites an existing file. The Qt interface does this at its start,
// so an account that starts with the Qt interface does not begin empty.
#pragma once
#include <string>

#include "config.hpp"
#include "profile_files.hpp"
#include "profile_migrate.hpp"
#include "seed.hpp"

namespace spn::ui {

// Switch for the split layout in the interface (docs/plan-config-split.md). The settings model reads and writes
// one file per profile (devices/, apps/) and dofscroll.conf for [global]; the migration of an old single file
// runs at the first start (under the instance lock) and the seeding writes the new layout.
constexpr bool kSplitLayoutInUi = true;

struct FirstStart {
    bool migrated = false;  // a legacy configuration was moved over
    std::string migrateLog;
    bool seeded = false;    // example configuration and/or modes were created
    std::string seedLog;
    MigrateResult split;    // migrateToSplit (status NothingToDo when it did not run)

    // Log lines for stderr, each ending in a newline. "who" is the program name.
    std::string text(const std::string &who) const {
        std::string t = who + ": config migration: " + (migrated ? "done" : "not needed or not possible");
        if (!migrateLog.empty()) t += " - " + migrateLog;
        t += "\n";
        if (split.status == MigrateStatus::Migrated) t += who + ": " + split.message + "\n";
        if (split.status == MigrateStatus::Failed) t += who + ": split migration failed: " + split.error + "\n";
        for (const std::string &w : split.warnings) t += who + ": split migration: " + w + "\n";
        if (seeded) t += who + ": seeded defaults: " + seedLog + "\n";
        return t;
    }
};

// The caller holds the single-instance lock. splitLayout: see kSplitLayoutInUi.
inline FirstStart runFirstStart(bool splitLayout = kSplitLayoutInUi) {
    FirstStart r;
    r.migrated = migrateLegacyConfig(&r.migrateLog);
    if (splitLayout) r.split = migrateToSplit(configDirOf(defaultConfigPath()));
    r.seeded = seedDefaults(&r.seedLog, splitLayout);
    return r;
}

}  // namespace spn::ui
