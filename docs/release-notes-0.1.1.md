<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 0.1.1

First numbered release after the release candidates and the betas. (The tag `v0.1.0` exists but was
never published: its package build failed with Qt 6.10, so nothing was released under that number;
0.1.1 has the same content plus the fix.) It builds on
[0.1.0-beta.2](release-notes-0.1.0-beta.2.md) (and [beta.1](release-notes-0.1.0-beta.1.md)); everything
that is not mentioned here is unchanged. GitHub marks every `0.x` version as a pre-release: this is a
version for everyone to try, not a promise of stability. Read "Tested on and known limitations" first.

## Highlights

- **One interface on every system.** The Qt program `dofscroll-ui` is the only tray icon and settings
  window on Linux, Windows and macOS. The old GTK tray (`dofscroll-tray`) is removed (#43). When you
  upgrade the Linux package, a user who had `dofscroll-tray.service` enabled gets
  `dofscroll-ui.service` enabled instead; only unit links change, never your configuration
  ("Upgrade" in [installation.md](installation.md)).
- **A new, profile-centric interface** ([ui.md](ui.md)): an Overview with one card per profile, then
  **Profiles** with *Global* first, then devices and programs; each profile has the tabs General,
  Channels, Buttons and Overrides. Everything is saved by itself, there is no Save button and no
  unsaved-changes dialog. Every value shows where it comes from ("Inherited from Global", "Set in this
  profile") with a reset per row; *Global* can set the joystick mode and assign buttons like any other
  profile. Modern look (rounded cards, light, dark and system theme), the window remembers its size,
  position and page, the interface starts minimized to the tray, closing it hides it to the tray (with a
  short notice), and wheel scrolling never changes a drop-down or a value.
- **Joystick modes and button modes, built in.** `normal`, `off`, `timeline`, `slides`, `media`,
  `map`, `precision`, `tabs` and `browse` (joystick modes) and `latch-nav`, `next-mode`, `prev-mode`,
  `cycle-favorites` and `reset-zoom` (button modes) ship inside the program on every system; your own
  file with the same id overrides them. Every mode has a short description (tooltips, info popover).
  Full list with what each one is for: wiki page *Built-in modes*.
- **Choose what a mode button cycles through.** A button mode with `mode:next` or `mode:prev` takes
  `CYCLE = a, b, c`: exactly these joystick modes in your order ([button-modes.md](button-modes.md)).
- **Split push and tilt.** A joystick mode can give push and tilt of the vertical and horizontal
  channel different functions (`V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`,
  per-part tuning keys; [modes.md](modes.md)). Modes without these keys behave exactly as before.
- **Key chords on buttons.** `BUTTON_<n> = key:ctrl+0` taps a chord once per press; used by the built-in
  button mode `reset-zoom` (Ctrl+0).
- **Documentation for modes** in the wiki: *Built-in modes*, *How to write a mode* (with recipes such as
  horizontal scrolling), *Mode requests and contributions* and *Supported systems and requirements*.
  Contributions of new modes: the "Mode proposal" issue form and the "new mode" pull request template.

## Changed

- `NAV_BUTTON` / `NAV_BUTTON_ENABLED` / `CLICK_BUTTON` / `CLICK_BUTTON_ENABLED` are deprecated and gone
  from the interface. The daemon still reads them. When the interface opens a configuration that still
  uses them it moves them once to `BUTTON_<n> = nav` / `click` (only where the device has that button;
  anything else stays untouched and is listed as "Not moved", see
  [configuration.md](configuration.md)).
- The interface no longer writes default values you did not set into `[global]` (a save added keys such
  as `STEP_TRIGGER`). Resetting a value in *Global* removes the key.
- The page *Channels* is now a tab of every profile; the page *Extensions* is gone (examples are
  built in; "New from example..." on the mode pages remains).
- Everywhere in the interface, the tray, the documentation and the wiki: **joystick mode** (what the axes
  do) and **button mode** (a reusable behavior for one button).
- First start only seeds the default configuration; mode files are no longer copied into your folders.
  An unchanged copy of a former example file is treated as the built-in. A profile that used
  `MODE = browse-keys` needs `MODE = browse`.
- On Linux the `mode:next` / `mode:prev` cycle runs over all loaded modes in alphabetical order, so it is
  long with the built-ins; use a `CYCLE` list.

## Added

- Mode library page: export, rename, "used by profiles", drag and drop import, "Create editable copy" for
  built-ins, descriptions; Export for profiles and button modes.
- Overview: documentation link, log viewer (last 64 KB with refresh and copy), quick actions, filter
  boxes, task buttons.
- Accessibility pass: contrast rules for text, borders, focus ring and selected rows tested for light and
  dark and for colour vision deficiencies, visible focus ring, high-contrast mode of the system is
  respected, font scaling test ([ui.md](ui.md), "Accessibility"; open items: issue #104).
- Windows and macOS: console output of `--help` / `--version`, no console window flash when the daemon is
  restarted, German catalog in the macOS app, template tray icon.
- Strict compile check for shadowed names on every platform (`-Wshadow`).

## Fixed

- A joystick mode named `release` could not be selected by a button.
- `ui.conf` is no longer reset when it cannot be read while saving the theme or the language.
- The selected row of the mode lists was unreadable.
- `activewin` looks for `kdotool` only in fixed system directories.

## Upgrading

Install the new package over the old one. Your configuration is never touched by packages; the first
start of the interface migrates old navigation/click button keys (see above). Per-OS steps:
[installation.md](installation.md). If you used `dofscroll-tray`, nothing to do: the package enables the
interface instead. A Qt 6 runtime is required for the interface (the packages declare it).

## Tested on and known limitations

- **Verified on real hardware by the maintainer:** Linux (RHEL 9, X11, KDE Plasma) with a SpaceMouse Pro and a
  SpaceMouse Compact, using the build of the same code from source. The packages of this release were
  built and install-tested in CI containers (Rocky Linux 9, Debian 12, others as listed in
  [installation.md](installation.md)); they were not installed on a physical machine.
- **Windows and macOS were never run with a real device.** The installers and the program are built and
  unit-tested by CI only, unsigned (SmartScreen and Gatekeeper warnings, [installation.md](installation.md)).
  Whether the official 3Dconnexion driver may stay installed next to dofscroll is unresolved
  ([wiki: Supported systems and requirements](https://github.com/HoZe-Lukas/dofScroll/wiki/Requirements)).
  Hardware tests: issues #36, #37, #38.
- Several SpaceMice on one computer do not work as separate devices on Linux (issue #93).
- The Qt mode editor has no controls for the split push/tilt keys yet (issue #100); edit the file.
- Open accessibility items: issue #104. The German texts were checked by the maintainer.
- Not verified in real programs: the keys that built-in modes send (page keys, `ctrl+0`, ...) depend on the
  program.

## Checksums

Verify downloads with `sha256sum -c --ignore-missing SHA256SUMS.txt` (protects against damaged
downloads, not against a compromised release).
