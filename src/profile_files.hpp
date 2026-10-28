// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// One configuration file per thing (plan docs/plan-config-split.md). Toolkit-free; used by the daemon, the
// UI and the config checker.
//
// Layout of a configuration directory:
//   dofscroll.conf          [global] (legacy [device:] / [app:] sections are still read)
//   devices/<slug>.conf     exactly one [device:NAME] section per file
//   apps/<slug>.conf        exactly one [app:NAME] section per file
//   modes/ buttons/         unchanged (modes.hpp, button_modes.hpp)
//
// Rules the code below implements:
//  - The header name is the truth, the file name is only a slug (profileSlug). A hand-renamed file is
//    read as usual and is rewritten under the canonical slug by the next save.
//  - Every profile exists once (kind and ASCII case-insensitive name). A file wins over a legacy section of
//    dofscroll.conf with the same name; between two files the first in sort order wins. The loser is
//    reported (FileDiag::fatal) and must never be written by the callers.
//  - A broken file (unreadable, too large, wrong header kind, no or several sections, [global] keys)
//    drops only its own profile. Per-file results are in the FileDiag list.
//  - Order of the sections in the loaded ProfileSet (this is the resolution order, "later wins"; ProfileSet::resolve
//    applies the sections in vector order): [global], then all devices, then all apps; inside a kind by PRIORITY
//    ascending (optional integer key of the profile, default 0, -1000..1000, no effect in [global]), ties:
//    legacy sections of dofscroll.conf first in file order, then files by file name (byte order). A pure legacy
//    single file without PRIORITY therefore resolves exactly as before; split files need PRIORITY (or file
//    names) to keep an order that matters (overlapping substring patterns). PROVISIONAL, may change.
//    assignMigrationPriorities() gives a legacy order to the split layout.
//  - Files are read through a directory handle without following links, owned by the current user, at most
//    kMaxProfileFiles per directory, kMaxProfileFileBytes each, at most kMaxSections - 1 profiles in total.
//  - Writes are temp file plus rename (fs::writeFileReplace), never through a link, never to a non-regular
//    file. Delete and rename check the stamp of the file as read, so a hand edit in between is reported
//    (WriteStatus::Conflict) instead of overwritten or lost. A rename writes the new file first and then
//    removes the old one: a crash in between leaves a duplicate (reported on the next load), never a loss.
//  - Paths are UTF-8 with '/' separators (platform/fs.hpp).
//
// Typical UI flow: loadProfileTree -> edit the ProfileSet -> for every changed section
// saveProfileFile(configDir, section, &diagOfThatSection), for a deleted one deleteProfileFile(...), for the
// global one saveGlobalFile(configDir, legacyPart(...), stamp) -> signal the daemon (SIGHUP) -> reload.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "config.hpp"

namespace spn {

constexpr size_t kMaxProfileFiles = 255;            // files read per directory (kMaxSections - 1)
constexpr size_t kMaxProfileFileBytes = 64 * 1024;  // size of one profile file
constexpr size_t kMaxProfileDirEntries = 16384;     // directory entries listed per directory (all are collected and sorted before the first kMaxProfileFiles are used, so the choice is deterministic)
constexpr size_t kMaxSlugBytes = 80;                // file name stem, without ".conf"

// ---- file names -------------------------------------------------------------------------------

// Canonical file name stem (without ".conf") for a profile name: ASCII case folded; the characters
// / \ : * ? " < > |, control characters (also U+0080..U+009F) and invalid UTF-8 bytes become '_'; leading and
// trailing dots and spaces become '_'; a reserved Windows name (CON PRN AUX NUL COM1-9 LPT1-9, also followed by
// an extension) gets a '_' after the reserved part; more than kMaxSlugBytes: cut at a UTF-8 boundary and
// "-<hash>" appended. Never contains a path separator or "..", never empty for a non-empty name.
std::string profileSlug(const std::string &name);
// The same with "-<hash>" always appended (used on a collision with another profile's file).
std::string profileSlugHashed(const std::string &name);
// 8 lower-case hex characters, FNV-1a 32 over the ASCII-folded name (same on every system).
std::string profileNameHash(const std::string &name);

// ---- loading ----------------------------------------------------------------------------------

struct FileDiag {
    enum Origin {
        GLOBAL_FILE,     // dofscroll.conf
        PROFILE_FILE,    // a file of devices/ or apps/
        LEGACY_SECTION,  // a [device:]/[app:] section inside dofscroll.conf
        DIRECTORY,       // a whole directory (too many files, cannot be opened)
    } origin = PROFILE_FILE;
    Section::Kind kind = Section::GLOBAL;  // DEVICE or APP for profile files and legacy sections
    std::string name;                      // profile name from the header; "" if the header is unusable
    std::string path;                      // file (LEGACY_SECTION: dofscroll.conf, DIRECTORY: the directory)
    std::string stamp;                     // profileFileStamp at read time (before reading); "" if none
    size_t section = ProfileSet::npos;     // index in the loaded ProfileSet; npos when not loaded
    std::vector<std::string> warnings;     // parser warnings and the reason of `fatal`
    // The profile of this entry is NOT in the ProfileSet (defective, duplicate, over a limit, or a legacy
    // section shadowed by a file). The UI must not overwrite a defective file while this stands.
    bool fatal = false;
};

// Reads <configDir>/dofscroll.conf, devices/ and apps/. A missing file or directory is no error. Returns
// false (and leaves *out untouched) only when dofscroll.conf exists but cannot be read (not a regular
// file, too large, I/O error): the caller keeps what it has. *diag (may be null) is cleared and filled in
// this order: one GLOBAL_FILE entry (always), one LEGACY_SECTION entry per legacy section (fatal = shadowed by
// a file or over a limit), one entry per file of devices/ and then apps/ (name order), and DIRECTORY entries
// for directory problems. Every profile file that exists has an entry (path, stamp, section index).
bool loadProfileTree(const std::string &configDir, ProfileSet *out, std::vector<FileDiag> *diag);
// Same for any global file path. devices/ and apps/ are read only next to a file named dofscroll.conf; a legacy
// location (e.g. spnav-scroll.conf in a generic config directory) is read as the single file it is.
// Also false when devices/ or apps/ exists but cannot be read (permission, not a directory, I/O error): the caller
// keeps its profiles; *diag then holds the fatal DIRECTORY entry.
bool loadProfileTreeAt(const std::string &globalFile, ProfileSet *out, std::vector<FileDiag> *diag);
// The single legacy file as today (loadProfiles), for readers that must not look at devices/ and apps/.
bool loadLegacyProfileFile(const std::string &file, ProfileSet *out, std::vector<std::string> *warnings);
// Directory part of a config file path ("." when there is none), '/' separators.
std::string configDirOf(const std::string &globalFile);

// ---- stamps -----------------------------------------------------------------------------------

// Identity of one file (mtime, ctime, size, file id) as a string; "" if it does not exist. follow = true also looks
// through a symlink (use it for dofscroll.conf, which may be a link into a dotfile repository), false looks at
// the entry itself (profile files are never links).
std::string profileFileStamp(const std::string &path, bool follow);
// Identity of everything the loader reads: dofscroll.conf plus name, mtime and size of every *.conf in
// devices/ and apps/. Cheap enough for 1x/s.
std::string profileTreeStamp(const std::string &configDir);
std::string profileTreeStampAt(const std::string &globalFile);

// Debounce for the poll: a change counts only when the same changed stamp is seen at two polls in a row
// (the UI writes two files in a row and then signals; a poll in between must not reload half a write).
//   StampDebounce d; d.reset(stampAtLoad); each poll: if (d.settled(profileTreeStampAt(f))) { reload; d.reset(...) }
class StampDebounce {
public:
    void reset(const std::string &applied) { applied_ = applied; seen_.clear(); }
    bool settled(const std::string &current);

private:
    std::string applied_, seen_;
};

// ---- writing ----------------------------------------------------------------------------------

enum class WriteStatus {
    Ok,
    Conflict,  // the file changed since it was read (stamp), or a foreign file of the same profile exists: nothing
               // was written / removed; the caller reloads and tells the user
    Refused,   // link or non-regular file at the target, or the name cannot be placed: nothing written
    Invalid,   // the section cannot be written (invalid name or key/value): nothing written
    Failed,    // I/O error
};
struct WriteResult {
    WriteStatus status = WriteStatus::Ok;
    std::string path;     // file written (or removed)
    std::string stamp;    // stamp of the file after a write
    std::string error;    // one line, for Conflict/Refused/Invalid/Failed
    std::string warning;  // Ok, but something is left over (e.g. the old file of a rename was changed by hand)
    bool ok() const { return status == WriteStatus::Ok; }
};

// Writes one profile into devices/ or apps/ (by sec.kind) under its canonical slug (profileSlug; with
// "-<hash>" if another profile owns that file name, also on a case-insensitive file system).
// `known` = the FileDiag the profile was loaded from (path and stamp), null for a new profile. If the file at
// `known->path` differs from the canonical path (rename of the profile, hand-renamed file), the new file is
// written first and the old one is removed afterwards, only if it is unchanged since it was read (otherwise
// WriteResult::warning). Writing over a file whose stamp differs from known->stamp is a Conflict. A new profile
// whose file name is taken by a file of the same profile is a Conflict too. leadingComment: optional
// "#" lines placed before the header (each line must start with '#'; used by the migration).
WriteResult saveProfileFile(const std::string &configDir, const Section &sec, const FileDiag *known,
                            const std::string &leadingComment = std::string());
// Removes a profile file after checking that it is unchanged since it was read (expectedStamp; "" skips the
// check). `path` must be a file directly in <configDir>/devices or <configDir>/apps (by kind).
// A file that is already gone is Ok.
WriteResult deleteProfileFile(const std::string &configDir, Section::Kind kind, const std::string &path,
                              const std::string &expectedStamp);
// Writes dofscroll.conf from the global section and the legacy sections that must stay in it (see
// legacyPart). expectedStamp: stamp (follow = true) of the file as read, "" skips the check. A symlink is
// followed and kept (dotfile repositories).
WriteResult saveGlobalFile(const std::string &configDir, const ProfileSet &globalAndLegacy,
                           const std::string &expectedStamp);
// Replaces dofscroll.conf by `text` through the same checks as saveGlobalFile (regular file, stamp unchanged).
WriteResult saveGlobalText(const std::string &configDir, const std::string &text, const std::string &expectedStamp);
// Global section plus the legacy sections of `ps` that are still active, i.e. what dofscroll.conf holds.
ProfileSet legacyPart(const ProfileSet &ps, const std::vector<FileDiag> &diags);
// For the migration: `ps` holds the sections in resolution order (the order of the legacy file, as
// loadProfiles/loadProfileTree return them). Sets PRIORITY 10, 20, 30, ... (a smaller step above 100 profiles, so the last stays within kMaxPriority) per kind in that order (replacing an
// existing PRIORITY) when the kind has more than one profile; a kind with one profile is left alone. After
// writeProfileSetSplit the split layout resolves identically to the old file.
void assignMigrationPriorities(ProfileSet *ps);
// Writes a whole ProfileSet in the new layout: every [device:]/[app:] section into its own file (new files or
// files of the same profile are overwritten without a stamp check; a file of another profile is never
// touched), then dofscroll.conf with [global] only. For the migration and the first-start seeding.
// *written (may be null) gets one FileDiag per written profile file. false + *err on the first failure
// (files already written stay; dofscroll.conf is written last, only when all profile files succeeded).
bool writeProfileSetSplit(const std::string &configDir, const ProfileSet &ps, std::vector<FileDiag> *written,
                          std::string *err);

}  // namespace spn
