<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# User interface

`dofscroll-ui` is the Qt6 tray icon and settings window, one code base for Linux, Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload. Checked by the maintainer only on Linux (RHEL 9, X11, KDE Plasma); the Windows and macOS builds are not run on hardware. Full text: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

## Pages

| Page | Content |
|---|---|
| Channels | Scroll, zoom and other output channels: sensitivity, dead zone, direction, curve preview |
| General | Output on/off, global mode, "Start at login", daemon status |
| Axes | Live bars of the six axes, axis assistant (output pauses while it is open) |
| Modes | Built-in and file modes: new, duplicate, import, edit, delete |
| Profiles | Device and application profiles, per-profile mode dropdown, overrides, button bindings |

## Profiles and the program picker

"Add application..." lists the programs that are running now (search by name, match string or window title, refresh) and shows the exact match string that will be stored. "Capture next focused program" takes the program you switch to. Precedence: global < device < mode overrides < application, see [Profiles](Profiles).

## Tray

Status, "Mode" submenu, "Enabled", "Settings...", "Open config file", "Show log...", "Open modes folder", "Quit". "Release all keys" exists only in the GTK tray for now; use `dofscroll --release-keys` otherwise.

## Start at login

One switch for the daemon and the interface: systemd user units on Linux, the per-user Run key on Windows, a LaunchAgent on macOS.

## Design principle

dofscroll complements the vendor driver instead of duplicating it: bindings that are the same everywhere and always belong in the driver; see [Buttons](Buttons).
