<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 0.1.0-beta.3

Pre-release (beta). Not a stable release. Builds on
[0.1.0-beta.2](release-notes-0.1.0-beta.2.md); the "Tested on" table and the known limitations of
beta.1 still apply.

## Changed

- **The Qt interface (`dofscroll-ui`) is the only tray and settings window** on Linux, Windows and
  macOS. **The old GTK tray (`dofscroll-tray`) is removed** (#43): the program, its unit
  `dofscroll-tray.service`, its icon directory, the `DOFSCROLL_TRAY` build option and the GTK 3 and
  AppIndicator build and run dependencies are gone. Packages no longer need GTK. **Autostart
  migrates:** when the package is upgraded, a user who had `dofscroll-tray.service` enabled gets
  `dofscroll-ui.service` enabled instead (and started if the tray was running); only unit links
  change, never the configuration. Details: "Upgrade" in [installation.md](installation.md).
- **The interface starts minimized to the tray**; no window opens. Click the tray icon or use
  "Open settings" to show it. Without a system tray the window opens minimized.
- Redesigned interface: light, dark and system theme, vertical category tabs with horizontal page
  tabs, status bar, and an **Overview** page with one card per profile, quick actions and
  "Getting started" steps for new users.
- Profiles show every button and the effective value of each setting with its origin (global,
  device, mode, program) and a one-click reset.

## Added

- **Button modes** with a page in the interface and an in-page **mode editor**; an **Extensions**
  page lists the mode files (plain files only, nothing executable).
- Button actions `toggle:nav` and `toggle:mode:<id>`, `mode:prev`, `hold:mode:<id>`, chords and
  `step:`; more example modes.
- Accessibility check of the interface (names, focus order, contrast).

## Fixed

- `activewin` looks for `kdotool` only in fixed system directories.

## Known limitations

Windows and macOS were never run with a real device, and the Qt interface was not checked on
Windows or macOS desktops (CI builds only). Hardware tests are tracked in the
[issues](https://github.com/HoZe-Lukas/dofScroll/issues).

## Checksums

Verify downloads with `sha256sum -c --ignore-missing SHA256SUMS.txt` (protects against damaged
downloads, not against a compromised release).
