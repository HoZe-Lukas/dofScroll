<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# The dofscroll interface (`dofscroll-ui`)

`dofscroll-ui` is the Qt6 interface: a tray icon plus a settings window, one code base for Linux,
Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload, so
changes apply immediately. The daemon runs without it. On Linux the older GTK tray
(`dofscroll-tray`) is still shipped; use one of the two, not both (see
[installation.md](installation.md)).

State: the pages below are merged on `main`. Behavior on a real desktop has been checked by the
maintainer only on Linux (RHEL 9, X11, KDE Plasma) with the SpaceMouse Pro and Compact; the
Windows and macOS builds come from CI and have not been run on hardware. See "Known limitations"
in the [readme](../readme.md).

## Window layout

The window has a navigation tree on the left and the selected page on the right. Entries that do
not depend on the active mode are always available:

| Page | Content |
|---|---|
| Channels | Scroll, zoom and the other output channels: sensitivity, dead zone, direction, curve preview |
| General | Output on/off, global mode, "Start at login", read-only status of the daemon |
| Axes | Live bars of the six axes and the axis assistant (wizard) that detects which axis does what |
| Modes | The available modes (built-in and files in the modes folder): new, duplicate, import, edit, delete; a reminder that mode files can press any key |
| Profiles | Per-device and per-program profiles, see below |

While the Axes page is open the daemon pauses its output, so moving the cap does not scroll the
window you look at.

## Mode dropdown

The Profiles page has a "Mode" dropdown per profile (global, device, application). A button can
also be bound to a mode switch (`mode:<id>`, `mode:next`) from the same page. Precedence is
unchanged: global < device < mode overrides < application. A profile that names a mode which does
not exist is shown with a warning instead of being changed silently.

## Profiles and the running-program picker

- Add a device profile from the list of connected devices.
- "Add application..." opens a picker of the programs that are running now: search by name, match
  string or window title, refresh, and see the exact match string that will be stored. You never
  have to type an internal name. "Capture next focused program" waits a few seconds and takes the
  program you switch to. A manual entry with validation is the fallback.
- Per profile you can override values (collapsed by default); "Remove" on an override makes the
  profile inherit the value again.
- Button bindings: a button is bound to a key, a mode switch or `nav`. Remember the principle: a
  binding that is the same everywhere and always belongs in the vendor driver, see
  [modes.md](modes.md#buttons-and-the-vendor-driver).

The program list comes from implementations of `src/platform/running_apps.hpp` for X11, Windows
(`EnumWindows`) and macOS (`NSWorkspace`); the Windows and macOS ones are only compile-checked.

## Tray menu

Status line, "Mode" submenu (the global mode), "Enabled", "Settings...", "Open config file",
"Show log...", "Open modes folder", "Quit". The entry "Release all keys" currently exists only in
the GTK tray; in a terminal use `dofscroll --release-keys` (see
[troubleshooting.md](troubleshooting.md#a-key-or-scrolling-gets-stuck)).

## Start at login

The General page has one switch for the daemon and the interface. It uses systemd user units on
Linux (`dofscroll.service`, `dofscroll-ui.service`), the per-user Run key on Windows and a
LaunchAgent on macOS. If the interface is not installed, only the daemon is switched. See
[installation.md](installation.md).

## Languages

English is the source language; the German catalog (`po/de.po`, `dofscroll_de.qm`) is installed
with the interface.
