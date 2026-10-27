// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Automatic migration of a single-file configuration to the split layout (plan docs/plan-config-split.md,
// "Migration"). Toolkit-free. The interface calls it at its start under the single-instance lock, right after
// migrateLegacyConfig; the daemon never migrates (it cannot write) but reads both layouts.
//
// migrateToSplit(configDir) does exactly this, in this order:
//   0. Only when <configDir>/dofscroll.conf is a regular file (a symlink: nothing is done, the single-file
//      format stays in use and is written as before) and holds at least one [device:] or [app:] section.
//   1. Copies the old file byte for byte to backup/dofscroll.conf.pre-split (never overwritten: an identical
//      earlier copy is reused, a different one makes the name ".pre-split.1", ".2", ...). The copy is read back.
//   2. Parses the old file, orders the profiles the way the loader resolves them (kind, PRIORITY ascending, file
//      order), gives them PRIORITY 10, 20, ... per kind (assignMigrationPriorities) and writes every profile that
//      has no file yet into devices/ or apps/. Comment lines directly above a section header move into the
//      new file. A profile that already has a file keeps that file (files win over legacy sections).
//   3. Reads everything back with loadProfileTree and compares it with what was parsed (same profiles, same
//      keys and values, same [global]; without pre-existing files also identical resolve results for probe
//      names derived from the patterns). Any difference or a defective new file aborts: the files written in this
//      run are removed again and dofscroll.conf stays untouched.
//   4. Only then replaces dofscroll.conf (atomically) by its non-profile part: the original text without the
//      profile sections, so the comments of [global] and any unknown section survive. If that text does not
//      parse back to the same [global] and no profile, a normalized [global] is written instead (the old text
//      stays in the backup). A final read-back compares the tree with step 3; on a difference the old file is
//      restored and the new files removed.
// Crash safe: every file is written by temp file plus rename. A crash before step 4 leaves the old file plus
// some complete new files; the next run reuses the backup, skips profiles that have a file and finishes.
// Idempotent: afterwards dofscroll.conf holds no profile section and the next call does nothing.
// Lines the parser skips (unknown keys, unknown sections) are never lost: unknown sections stay in dofscroll.conf
// when the text can be kept, everything stays in the backup, and MigrateResult::warnings lists them.
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace spn {

enum class MigrateStatus {
    NothingToDo,     // no dofscroll.conf, or no [device:]/[app:] section in it (already split, or only [global])
    Migrated,        // done; message holds the line the interface shows once
    SkippedSymlink,  // dofscroll.conf is a symlink: single-file format stays (nothing changed)
    Failed,          // nothing was changed in dofscroll.conf (see error); a backup copy may exist
};

// Test hook: called before each step; a return value other than None stops there.
//   Crash: return at once without any clean-up (the process "died"), Fail: act as if the step failed.
// Steps: "backup", "profile-file" (before each profile file), "readback", "replace-global", "final-check".
enum class MigrateFault { None, Crash, Fail };

struct MigrateOptions {
    bool backup = true;  // false only for the first-start seeding (the old file is the shipped example)
    std::function<MigrateFault(const std::string &step)> fault;
};

struct MigrateResult {
    MigrateStatus status = MigrateStatus::NothingToDo;
    std::string message;     // one informational line for the interface (Migrated, SkippedSymlink), else ""
    std::string error;       // one line for Failed
    std::string backupPath;  // backup/dofscroll.conf.pre-split[.N]; "" if none was made
    size_t profilesMoved = 0;         // profile files written by this run
    size_t profilesAlreadyFiled = 0;  // legacy sections whose profile already had a file (the file was kept)
    std::vector<std::string> filesWritten;  // paths of the profile files written by this run
    std::vector<std::string> warnings;      // lines the parser skipped (kept in the backup), other notes
    bool ok() const { return status != MigrateStatus::Failed; }
};

// configDir: the directory that holds dofscroll.conf (as configDirOf(defaultConfigPath())).
MigrateResult migrateToSplit(const std::string &configDir, const MigrateOptions &opt = MigrateOptions());

}  // namespace spn
