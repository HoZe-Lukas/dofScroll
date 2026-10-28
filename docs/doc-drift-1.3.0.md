<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Documentation drift before 1.3.0

Written 2026-10-28 against `main` at `03fcbaa`. Statements in the wiki sources (`docs/wiki/`), the packaging READMEs
(`packaging/*/README*`), `readme.md` and some `docs/` pages that contradict the current code or are incomplete after the
changes since 1.2.3 (config split, program `BUTTON_<n>` precedence, lift, key engine, take over, import). Each row gives the
statement, what the code does, and a proposed fix. **Nothing was changed in the files listed here**; this list is for the
release check of `docs/releasing.md` (step 2). It comes from reading the files named below against the code, not from a
complete line-by-line audit of all documents: absence from the list is not proof that a statement is right.

Files that other work is changing (`docs/ui.md`, `docs/modes.md`, `docs/configuration.md`, `site/`, `po/`) are listed in
their own section as findings only.

## A. Wiki sources (`docs/wiki/`)

| # | Where | Statement | What the code does | Proposed fix |
|---|---|---|---|---|
| A1 | `Profiles.md:7` | "Profiles are sections in `dofscroll.conf` that override only the keys they list." | The interface writes one file per profile: `devices/<slug>.conf` (one `[device:NAME]`) and `apps/<slug>.conf` (one `[app:NAME]`); `dofscroll.conf` keeps `[global]`. Sections of a single file are still read ("legacy sections", a file wins). `src/profile_files.hpp` (header), `docs/plan-config-split.md` | Say that a profile is a file in `devices/` or `apps/` (a section of an old single file still works), name the automatic migration with the backup `backup/dofscroll.conf.pre-split`, and link the release notes |
| A2 | `Profiles.md:21` | "All device sections are applied before all app sections, each in file order, so a later matching section wins." | Inside a kind the order is the optional `PRIORITY` ascending (integer, -1000 to 1000, default 0), ties: legacy sections in file order, then files by file name. `src/profile_files.hpp:21-27`, `src/config.hpp:257` | Replace "in file order" by the `PRIORITY` rule; mention Move up / Move down in the interface |
| A3 | `Profiles.md:57` | "Only the action is copied, not the tuning of the channel." | The Take over dialog has the check box "Also take over the tuning" (off by default) that copies sensitivity, dead zone, direction, curve and smoothing (#256) | "By default only the action is copied; the check box ... also copies the tuning" |
| A4 | `Profiles.md` (whole page) | No mention of import, export as a file copy beyond one clause, defective profile files, `PRIORITY` or the migration | Import with safety checks, defective files as own list entries, Move up/down, rename, delete (#222, #237) | Add a short section "Profile files" (list the above, link `docs/ui.md`) |
| A5 | `Configuration-reference.md:15-18` (file table) | Lists `dofscroll.conf`, `modes/`, legacy paths, Windows path only | Also `devices/`, `apps/`, `buttons/` (button modes), `backup/` (migration), `ui.conf` | Add rows for `devices/*.conf`, `apps/*.conf`, `buttons/*.conf`, `backup/` |
| A6 | `Configuration-reference.md:20` | "The tray rewrites the file without keeping comments" | The interface (`dofscroll-ui`) writes the changed profile files and `dofscroll.conf` only if its part changed (`saveProfileFile`, `saveGlobalFile`); the header comment of the written `dofscroll.conf` is fixed in the open PR #281 | "The interface rewrites the file it changes ..." |
| A7 | `Configuration-reference.md:22` | "Limits: file up to 1 MiB, ..., 512 keys per section" | True for `dofscroll.conf` (`kMaxConfigBytes`). A profile file is limited to 64 KiB (`kMaxProfileFileBytes`, `src/profile_files.hpp:48`) and 255 files per directory (`kMaxProfileFiles`) | Add the two limits |
| A8 | `Configuration-reference.md:26-37` | "Three channels ... `SCROLL_V`, `SCROLL_H`, `ZOOM`", the table has only `<CH>_*` | Six motions: the push and tilt parts of V and H (`SCROLL_<V\|H>_<PUSH\|TILT>_*`), twist (`ZOOM_*`) and lift (`LIFT_*`, `LIFT_ACTION`, `LIFT_MIX` default `y`, `LIFT_DEADZONE` default 80); `<P>KEY_RATE_MIN`, `<P>KEY_RATE_MAX`, `<P>TRIGGER`; profile keys `V_ACTION`, `H_ACTION`, `TWIST_ACTION`, `V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`, `LIFT_ACTION`; `PRIORITY` (`docs/configuration.md` already documents most of it) | Add the keys above (or one table row each linking `docs/configuration.md`) |
| A9 | `Buttons.md:31` | "a binding in the active joystick mode wins over the main config for the same button" | True for `[global]` and `[device:*]`. An `[app:*]` `BUTTON_<n>` now wins over the joystick mode's binding (#215), and `BUTTON_<n> = none` there is an explicit unbind | Add "except in an `[app:*]` section, where the program's binding wins" |
| A10 | `Modes.md:112`, `Writing-modes.md:391` | "A joystick mode binding wins over a `BUTTON_<n>` binding in the main config" | Same as A9 | Same wording as A9 |
| A11 | `Quick-start.md:17` | "On first start the tray writes `dofscroll.conf` from `examples/default/<os>/...`"; "it adds the profile of that device" | The interface seeds `dofscroll.conf`; the first device profile is written as `devices/<slug>.conf` (`writeFirstDeviceProfileFile`, #220/#222) | "the interface writes ... and `devices/<name>.conf` for your device" |
| A12 | `Roadmap.md:7`, `:13-37`, `:39-45` | "state as of the 0.1.0-beta.1 documentation pass"; the Done list ends at the Qt interface; Planned lists only W9, W7/W8 | Done since: config split, lift, key engine, take over, import, axis assistant rework, device selector. Planned: motion functions stages 4 to 7 (`docs/plan-motion-functions.md`, #224, #225): `pointer_x/y`, click and `hold:`, presets, macro, own functions | Update both lists and the date of the state; do not list planned items as done |
| A13 | `Architecture.md:15`, `:33` | "writes config, asks for reload"; pure logic list `config`, `dsp`, `modes`, `mode_runner`, `profile_match`, `device_db`, `hid_descriptor`, `key_tables`, `seed` | New toolkit-free parts: `profile_files`, `profile_migrate`, `key_engine`, `axis_convention`, `mouse_move_math`; the UI writes several files | Add the modules; "writes profile files" |
| A14 | `User-interface.md:57`, `Built-in-modes.md:5`, `Contributing-modes.md:42`, `Writing-modes.md:44` | "the three channels" of a joystick mode | A joystick mode can set six motions (lift included) and push/tilt separately; the "three channels" are the scroll/zoom tuning groups | "the motions (three channels, split into parts, and the lift)" |
| A15 | `User-interface.md:117` | "If the file is changed outside (a text editor, the tray), the window follows it" | The tray is part of the same program; the daemon polls a directory stamp of all profile files | "(a text editor, another program)" |
| A16 | `User-interface.md:135` | Profile editor tabs and "Export..." only | Import profile, New profile, Take over, Move up/down, rename, delete, defective files (see A4) | Add one sentence each and link `docs/ui.md` |

## B. Packaging READMEs

| # | Where | Statement | What the code does | Proposed fix |
|---|---|---|---|---|
| B1 | `packaging/windows/README.md:5` | "There is no installer yet (package W4)." | `packaging/windows/installer.nsi` builds an unsigned per-user installer and ZIP in CI (`.github/workflows/installers.yml`); the README lists `stop-ui.ps1` and `stop-daemon.ps1` as "the installer uses it" in the same file | "These scripts are used by the installer and can be used for a manual setup ..." |
| B2 | `packaging/windows/README.md:16` | "All three accept `-WhatIf`" | The table lists four scripts | "All scripts accept ..." (check that `stop-ui.ps1`/`stop-daemon.ps1` really accept `-WhatIf`) |
| B3 | `packaging/macos/README.md:5` | "There is no installer, `.pkg` or signed app bundle yet (work package W4); everything here works from a local build." | `packaging/macos/build-dmg.sh` and the `macOS DMG` job build an unsigned, not notarized DMG (Apple silicon); this README is about running a local build as a LaunchAgent | "There is an unsigned DMG (see installation.md); this page covers the LaunchAgent for a local build" |

## C. `readme.md`

| # | Where | Statement | What the code does | Proposed fix |
|---|---|---|---|---|
| C1 | `readme.md:41` | "`dofscroll-ui` ... Writes the same config file and tells the daemon to reload." | Writes `dofscroll.conf` and one file per profile in `devices/` and `apps/`, then the daemon reloads when the directory stamp changes | "Writes the configuration files ..." |
| C2 | `readme.md:49` and the status table | "Current stable version: 1.2.3" and the platform table | Matches `CMakeLists.txt` (1.2.3) and `docs/release-notes-1.2.3.md` today | Update at the release (ties to `docs/releasing.md`); not drift now |

## D. Findings in files that other work owns (not fixed here)

| # | Where | Statement | What the code does | Proposed fix |
|---|---|---|---|---|
| D1 | `docs/configuration.md:176` | "All device sections are applied before all app sections, each in file order" | `PRIORITY` order (see A2). The page also does not describe the split layout (`devices/`, `apps/`), the migration or the `PRIORITY` key (grep finds none in `configuration.md`, `modes.md`, `ui.md`) | Add a section "One file per profile" and the `PRIORITY` rule |
| D2 | `docs/modes.md:187`, `docs/button-modes.md:94` | "A binding in the active joystick mode ... wins over `BUTTON_<n>`" / "The precedence does not change" | Program profile wins (`docs/configuration.md:119` and `:192`, `docs/modes.md:137` already say so) | Make the three statements agree; qualify with "except in `[app:*]`" |
| D3 | `docs/status-release-1.2.4.md` | "Open pull requests and the order to merge them: #232, #222, #221, #217"; "Not started: #224" | #232, #222, #221 are merged; #224 stages 1 to 3 and the output layer of stage 5 are merged; the release is planned as 1.3.0 in `docs/release-notes-1.3.0-draft.md` | Rewrite as a status of the real state, or delete when the release notes exist |
| D4 | `src/profile_files.hpp:25` | "PROVISIONAL, may change" for the PRIORITY order | The order is implemented, tested and described in `docs/plan-config-split.md` | Decide before the release whether it is final; if so remove the word (this row is a header comment, not a doc page) |

## E. Checked and consistent (as of `03fcbaa`)

Version 1.2.3 in `readme.md`, wiki Home / Requirements / FAQ / Roadmap status lines and `CMakeLists.txt`; Qt 6.9.3 in the wiki
Requirements and Installation pages and `packaging/qt.env`; CMake 3.16 and Qt 6.4 as build minimums (`CMakeLists.txt:22`,
`cmake/Ui.cmake:9`); factory button counts in `Buttons.md` against `src/device_db.cpp` (Compact 2, Wireless 2, Pro 15, Pro
Wireless 15, Enterprise 31, SpaceNavigator 2); `BUTTON_<n>` 1-based and at most 32 (`kMaxButtons`); the media mode twist as
`step:j,l` in `Built-in-modes.md:26` (`examples/modes/media.conf`); the limits 256 sections, 512 keys, 8 KiB lines,
1 MiB for `dofscroll.conf` (`src/config.hpp`). Not checked: the German website, `site/`, the screenshots, the wiki pages
Installation / Requirements beyond the lines above, and every statement about Windows and macOS behavior.
