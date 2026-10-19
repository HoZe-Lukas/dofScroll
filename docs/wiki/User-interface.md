<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# User interface

`dofscroll-ui` is the Qt6 tray icon and settings window, one code base for Linux, Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload. Checked by the maintainer only on Linux (RHEL 9, X11, KDE Plasma); the Windows and macOS builds are not run on hardware. Full text: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

## Pages

Vertical tabs on the left pick a category, horizontal tabs on top pick the page inside it. The status bar at the bottom shows the daemon state.

| Category | Page (tab) | Content |
|---|---|---|
| Profiles | Profiles | Device and application profiles, per-profile mode dropdown, overrides, button bindings |
| Profiles | Channels | Scroll, zoom and other output channels: sensitivity, dead zone, direction, curve preview |
| Modes | Modes | Built-in and file modes: new, duplicate, import, edit, delete |
| Device | Axes | Live bars of the six axes, axis assistant (output pauses while it is open) |
| Settings | General | Output on/off, global mode, "Start at login", daemon status |

## Appearance

Settings > Appearance: System default, Light or Dark. It applies at once and is remembered (`ui.conf`, `theme = system|light|dark`). Text keeps a contrast of at least 7:1 (4.5:1 for hints, links and the selection) in both themes.

## Profiles and the program picker

"Add application..." lists the programs that are running now (search by name, match string or window title, refresh) and shows the exact match string that will be stored. "Capture next focused program" takes the program you switch to. Precedence: global < device < mode overrides < application, see [Profiles](Profiles).

## Tray

Status, "Mode" submenu, "Enabled", "Settings...", "Open config file", "Show log...", "Open modes folder", "Release all keys" (runs `dofscroll --release-keys`, as in the GTK tray), "Restart daemon" (releases held keys, then restarts the daemon: systemd on Linux, launchd on macOS, stop and start on Windows), "Restart interface" (restarts the interface itself), "Quit".

## Saving and the daemon

Saving notifies the running daemon: SIGHUP on Linux, a named event on Windows, a local socket on macOS (the last two are CI-verified only). The device list on the Profiles page and the factory button count (Profiles and Axes pages) come from the daemon's live feed.

## Start at login

One switch for the daemon and the interface: systemd user units on Linux, the per-user Run key on Windows, a LaunchAgent on macOS.

## Design principle

dofscroll complements the vendor driver instead of duplicating it: bindings that are the same everywhere and always belong in the driver; see [Buttons](Buttons).
