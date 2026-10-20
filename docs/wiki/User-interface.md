<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# User interface

`dofscroll-ui` is the Qt6 tray icon and settings window, one code base for Linux, Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload. It starts minimized to the tray (no window opens); use the tray's "Settings..." to show it. Without a system tray the window is shown minimized, and closing it quits the interface (the status bar says so). With a tray, closing the window hides it to the tray (a short notice appears the first time); the tray's "Quit" ends the interface. The window remembers its size, position and last page (`ui.conf`). Checked by the maintainer only on Linux (RHEL 9, X11, KDE Plasma); the Windows and macOS builds are not run on hardware. Full text: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

## Pages

Vertical tabs on the left pick a category, horizontal tabs on top pick the page inside it. The status bar at the bottom shows the daemon state.

| Category | Page (tab) | Content |
|---|---|---|
| Overview | Overview | Only summarizes and links: daemon state, task buttons (tune scroll and zoom = the Channels tab of Global, check the axes, switch joystick modes), quick actions (active joystick mode, open log, documentation, new profile for the running program, release keys, restarts) and one card per profile (joystick mode, what it overrides and how many settings, button bindings included) with a filter box; "Open" jumps to the profile |
| Profiles | Profiles | The profile list (Global first, then devices, then programs) and the editor of the selected profile: General (joystick mode, output switch), Channels (scroll, zoom, curve preview, zoom detection), Buttons and Overrides. The same for Global |
| Joystick modes | Joystick modes | Built-in and file joystick modes with origin and description: new, new from example, duplicate, import, export, edit, delete |
| Joystick modes | Button modes | Button mode files (`buttons/`): new, new from example, edit in the page, duplicate, import, delete; chosen per button on the Profiles page |
| Device | Device | The connected devices, their button count and the daemon state |
| Device | Axes | Live bars of the six axes, axis assistant (output pauses while it is open) |
| Settings | Settings | Output on/off, start at login, language, theme, tick rate and output backend, daemon status, "Show log...", "Documentation" |

Nothing in the window has a Save button: every change is saved by itself (switches and drop-downs at once, typed text after about 600 ms or when you leave the field) and the daemon reloads; the footer says "Saved" or "Could not save: ...". An invalid value is not written, the problem is shown next to the field. Closing the window and Quit write what is pending first.

"Show log..." (Overview "Open log", Settings page, tray) opens a small window with the end of the daemon log (Refresh, Copy). "Documentation" opens the project documentation in your browser. "Active joystick mode" on the Overview switches the running daemon to another joystick mode until the next reload or restart (the same request as the tray's Joystick mode menu). Every joystick mode and button mode shows its description as tooltip, as a second line in the lists and in the **i** popover next to the joystick mode drop-down of a profile. The axes step of "Getting started" is done once the axis assistant has been applied.

## Appearance

Settings > Appearance: System default, Light or Dark. It applies at once and is remembered (`ui.conf`, `theme = system|light|dark`). Text keeps a contrast of at least 7:1 (4.5:1 for hints, links and the selection) in both themes. Selected rows, focus rings (2 px) and control borders are tested for contrast too, status colours are checked for colour-vision deficiencies, the interface follows the system font size, and a high-contrast request of the system is honoured (see `docs/ui.md`, section Accessibility).

## Profiles and the program picker

The editor of a profile (also Global) has the tabs General, Channels, Buttons and Overrides; the header shows which profile is edited (icon, name, kind) with one line about inheritance, the tab stays when you select another profile, and the list stays visible. Every setting row and button row says where its value comes from ("Inherited from Global: 5", "Set in this profile (inherited: 5)") and has a Reset; in Global a value that profiles override says "N profiles override this". "Export..." writes the selected profile as a `.conf` file. A search box above the list filters the profiles by name, one above the tabs filters the settings by label or key. The joystick mode editor has the tabs Settings, Buttons and Overrides in the same way and saves by itself. See [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

"Add application..." lists the programs that are running now (search by name, match string or window title, refresh) and shows the exact match string that will be stored. "Capture next focused program" takes the program you switch to. Precedence: global < device < joystick mode overrides < application, see [Profiles](Profiles).

## Tray

Status, "Joystick mode" submenu, "Enabled", "Settings...", "Open config file", "Show log...", "Open joystick modes folder", "Release all keys" (runs `dofscroll --release-keys`), "Restart daemon" (releases held keys, then restarts the daemon: systemd on Linux, launchd on macOS, stop and start on Windows), "Restart interface" (restarts the interface itself), "Quit".

## Saving and the daemon

Every change notifies the running daemon: SIGHUP on Linux, a named event on Windows, a local socket on macOS (the last two are CI-verified only). The device list on the Profiles page and the factory button count (Profiles and Axes pages) come from the daemon's live feed.

## Start at login

One switch for the daemon and the interface: systemd user units on Linux, the per-user Run key on Windows, a LaunchAgent on macOS.

## Design principle

dofscroll complements the vendor driver instead of duplicating it: bindings that are the same everywhere and always belong in the driver; see [Buttons](Buttons).
