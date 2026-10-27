<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Plan: one configuration file per thing

Status: decided, not implemented. Stages 1 to 5 below are separate pull requests; stage 1 starts after #215
(profile editing) is merged.

## Goal

Today everything is in one file (`dofscroll.conf` with `[global]`, `[device:NAME]`, `[app:NAME]`). The
maintainer wants every thing in its own file, cleanly separated: the global settings, every device profile and
every program profile. Joystick modes and button modes already are separate files under `modes/` and `buttons/`.

Only the persistence changes. The in-memory `ProfileSet`, the key syntax, the precedence (Global < device <
joystick mode overrides < program), the DSP, the modes and the UI logic stay as they are.

## Layout

    <config dir>/
      dofscroll.conf          [global] only (process-wide keys such as INPUT stay where scripts expect them)
      devices/<slug>.conf     one [device:NAME] section per file
      apps/<slug>.conf        one [app:NAME] section per file
      modes/  buttons/        unchanged
      backup/dofscroll.conf.pre-split     the old single file, kept by the migration

- A profile file has the same syntax as today and exactly one section whose header carries the real name
  (`[device:SpaceMouse Pro]`). The header is the truth, the file name is only a slug. This is the format that the
  profile export already writes, so an exported profile is a valid profile file and import is a file copy.
- Every profile exists once. The file system already forbids two files with the same name; two files whose header
  names are the same (for example a hand-made copy) are an error: the first file in sort order wins, the second is
  reported as defective in the UI and the log and is never written.
- A file in `apps/` with a `[device:...]` header, or with several sections, is defective (warning, ignored).
- The single-file format stays readable for ever (hand-edited files, dotfile repositories with a symlinked
  `dofscroll.conf`). Sections `[device:]` and `[app:]` inside `dofscroll.conf` are still accepted; when the same
  profile also has a file in `devices/` or `apps/`, the file wins and the section is ignored with a warning.

## Order of overlapping profiles (PRIORITY)

In the single file the order matters: all device sections are applied before all app sections, each in file order,
and a later matching section wins (matching is by substring, so several app profiles can match the same program).
Files have no order of their own, so every profile may carry an optional integer key `PRIORITY` (range -1000 to
1000, default 0, no effect in `[global]`). Profiles of the same kind are applied by `PRIORITY` ascending (later wins
as today); ties: legacy sections first (in file order), then the files by file name. `ProfileSet::resolve` is
unchanged, it applies the sections in vector order. The migration assigns `PRIORITY` 10, 20, 30 ... in the old file
order when a kind has more than one profile, so the split layout resolves exactly like the old file (tested with
overlapping patterns). The UI shows the order and offers move up and move down (stage 4).

A defective profile file does not hide a legacy section of the same name: the section stays active, so mixed versions
lose nothing; the UI says so next to the profile. If two profiles collide on a file name, the profile that is written
second gets the hash suffix (the result depends on which file exists first, which is acceptable).

## File names (slug)

- The slug is the name with ASCII case folding (`src/utf8.hpp`); the characters `/ \ : * ? " < > |`, control
  characters and a leading dot or space become `_`; UTF-8 bytes are kept (no transliteration). A trailing dot or
  space becomes `_`. The reserved Windows names (`CON PRN AUX NUL COM1-9 LPT1-9`) get a `_` appended.
- At most 80 bytes, cut at a UTF-8 boundary. On a cut or a collision (also a case-fold collision on the case-
  insensitive file systems of Windows and macOS) `-<8 hex characters of a hash of the folded name>` is appended; the
  hash is always taken over the folded name, so the slug is the same on every system and a copied config stays
  portable.
- The slug is never taken from input as a path. Files are opened in the fixed directory only, without following
  links, never with `..`. On load only the header name counts; a hand-renamed file is still read and is written
  under the canonical slug the next time.

## Loading

One shared function (`loadProfileTree`) for the daemon, the UI and the config checker: read `dofscroll.conf`, then
list and read `devices/` and `apps/` (bounded: file count, size per file, total count of profiles), every file on
its own through the existing parser. A broken file drops only its profile; the others stay active. The result
carries per-file diagnostics, which the profile tree shows next to the profile ("defective": the UI never overwrites
a defective file while the error stands).

Reload: SIGHUP, the Windows event and the macOS socket stay. The poll stamp becomes a directory stamp over
`dofscroll.conf`, `devices/` and `apps/` (like `modes/` today); no file watching API. A change of one file reloads
everything (cheap). The stamp is debounced by one poll interval because the UI may write two files in a row.

## Writing (UI)

The settings model keeps a dirty state per section (also for deleted and renamed profiles). Autosave writes only
the changed files, each with temp file plus rename (`fs::writeFileReplace`), then one reload.

- Delete: remove the file, after checking that it is unchanged since it was read (otherwise: report, no loss).
- Rename: write the new file first, then remove the old one; a crash leaves a duplicate, never a loss.
- Before writing, the stamp of the target is compared; if somebody edited the file by hand in the meantime the UI
  says so and reloads instead of overwriting. No lock files (they do not fit hand editing).

## Migration (automatic)

At the first start of the UI of the new version, directly after `migrateLegacyConfig`, under the instance lock.
The daemon never migrates (it may not write) but reads both layouts, so a start without the UI keeps working.

1. Copy the old file byte for byte to `backup/dofscroll.conf.pre-split` (never overwrite, number if it exists).
2. Write every `[device:]` and `[app:]` section into its own file; comments directly above a section move with it.
3. Read everything back and compare it with the original `ProfileSet`; any difference aborts the migration and
   leaves the old file untouched. "Never lose a key" means exactly this check.
4. Only then replace `dofscroll.conf` by its `[global]` part, atomically.

If `dofscroll.conf` is a symlink the migration is not done (the file is only read and the UI keeps writing the
single-file format). The first-start seeding writes the new layout (global file, first device profile as
`devices/<slug>.conf`) and counts `devices/` and `apps/` as "already configured".

## Downgrade

Manual and documented in the release notes: an older version reads only `dofscroll.conf`, so after the migration it
sees `[global]` and no profiles (nothing is lost). To go back:
`cat devices/*.conf apps/*.conf >> dofscroll.conf`, or restore `backup/dofscroll.conf.pre-split`. No extra UI.

## Stages (each its own pull request, with tests)

1. **Loader** with legacy support: `loadProfileTree`, the slugger, the directory stamp, per-file diagnostics, the
   daemon uses it; `check_config` takes a directory. Tests: slug table (Windows reserved names, collisions, length,
   UTF-8, case folding), isolation of a broken file, duplicate headers, precedence of file over section, limits.
2. **Writer**: per-file writes, delete, rename, stamp check; model and tests only (crash between two writes,
   collision, refusing a link as target).
3. **Migration**: `migrateToSplit` with backup, read-back check, symlink exception, idempotence, tests with the
   files of `examples/`; seeding writes the new layout.
4. **UI**: per-file dirty state, defective files in the profile tree, delete, rename, export as file copy, import;
   texts in `po/` (German with "du").
5. **Docs, examples, packaging, CI**: `docs/configuration.md`, `ui.md`, `security.md`, `installation.md`,
   `troubleshooting.md`, `multi-device.md`, the wiki, `site/` and `site/de/`, `examples/default/<os>`,
   `cmake/PackageLayoutTest.cmake`, postinst tests, the release notes with the downgrade note.

What only a human can verify on real hardware: the first start on a fresh account and the upgrade of a real
configuration on every operating system, the tray and autosave behavior, the first device profile when the device
is plugged in first, and the installer upgrade on Windows and macOS (CI only builds them).

## Risks

- Case-insensitive file systems (Windows, macOS): the hash suffix from the folded name solves collisions; the slugger
  must give the same result everywhere.
- Windows specials: reserved names, trailing dots, path length (80 bytes of slug are usually fine), alternate data
  streams (`:` is replaced); names must go through `fs_win.cpp`, not `fopen`.
- Mixed versions during an upgrade (new daemon, old UI): an old UI writes only `dofscroll.conf` and would drop the
  profiles from the single file while `devices/` stays; the rule "a file wins over a section" covers that.
- Not yet looked at in detail: `packaging/cpack/postinst`, the Windows and macOS installer upgrade paths and
  `tests/test_postinst_migration.sh` (stage 3 and 5).
