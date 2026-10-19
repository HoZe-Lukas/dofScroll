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

The window has no sidebar or tree. Vertical tabs on the left pick a category; where a category has
several pages, horizontal tabs on top of the content pick the page (a category with one page shows
no tab bar). A status bar at the bottom shows the daemon state (the same line as the tray's first
entry). The grouping comes from `src/ui/model/navigation_model.hpp`, the registry
(`pageFactories()`) says which category a page belongs to:

| Category | Page (tab) | Content |
|---|---|---|
| Profiles | Profiles | Per-device and per-program profiles, see below |
| Profiles | Channels | Scroll, zoom and the other output channels: sensitivity, dead zone, direction, curve preview |
| Modes | Modes | The available modes (built-in and files in the modes folder): new, duplicate, import, edit, delete; a reminder that mode files can press any key |
| Device | Axes | Live bars of the six axes and the axis assistant (wizard) that detects which axis does what |
| Settings | General | Output on/off, global mode, "Start at login", read-only status of the daemon |

This is the first step of the redesign (#52): the pages are unchanged, only their place moved.
`dofscroll-ui --screenshots <dir>` renders every page at three window sizes into PNG files (use
`QT_QPA_PLATFORM=offscreen` on a machine without a display); CI attaches them to the UI job.

While the Axes page is open the daemon pauses its output, so moving the cap does not scroll the
window you look at. The page shows as many button cells as the factory button count of the
device (see below), or the highest button it has seen when no device information is available.

## Mode dropdown

The Profiles page has a "Mode" dropdown per profile (global, device, application). A button can
also be bound to a mode switch (`mode:<id>`, `mode:next`) or a toggle (`toggle:nav`, `toggle:mode:<id>`) from the same page; the tray shows "Navigation mode on" while the navigation mode is latched. Precedence is
unchanged: global < device < mode overrides < application. A profile that names a mode which does
not exist is shown with a warning instead of being changed silently.

## Profiles and the running-program picker

- Add a device profile from the list of connected devices. The list and the factory button count
  (which limits the buttons you can bind) come from the daemon's live feed; without the feed the
  page falls back to the `device=` line of the status file.
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
"Show log...", "Open modes folder", "Release all keys", "Restart daemon", "Restart interface",
"Quit". "Release all keys" starts
`dofscroll --release-keys` as a separate process (the daemon binary next to the interface, else
`dofscroll` from `PATH`), like the GTK tray; the same command works in a terminal (see
[troubleshooting.md](troubleshooting.md#a-key-or-scrolling-gets-stuck)).

"Restart daemon" releases held keys first (as "Release all keys"), then restarts the daemon:
on Linux `systemctl --user restart dofscroll.service` when the unit is active, on macOS
`launchctl kickstart -k gui/<uid>/io.github.dofscroll.daemon` when the LaunchAgent is loaded; on
Windows (and when the service manager does not manage the daemon) it stops the process (Windows:
`scripts\stop-daemon.ps1`) and starts the `dofscroll` binary next to the interface. "Restart
interface" restarts this program: under `dofscroll-ui.service` through
`systemctl --user restart dofscroll-ui.service`, otherwise it starts a copy of itself with
`--restart` (which waits up to 8 seconds for the old instance to release the single-instance
lock) and quits. No shell is involved; the command lines are built in
`src/ui/model/restart_plan.hpp`. Untested on real desktops for Windows and macOS.

## Reload and runtime files

Saving in the settings window notifies the running daemon: SIGHUP on Linux, a named event on
Windows, a per-user local socket on macOS. The status, log and mode-request files live in one
per-user runtime directory that daemon and interface agree on (`$XDG_RUNTIME_DIR` on Linux,
`%LOCALAPPDATA%\dofscroll` on Windows, `~/Library/Caches/dofscroll` on macOS). The interface
reads the status file only when it is bounded in size, not a symlink and owned by the current
user; without a safe directory the daemon counts as not running. While the Axes page measures,
the interface sends a heartbeat so the daemon keeps its output paused. The Windows and macOS
paths are CI-verified only, not run on hardware.

## Start at login

The General page has one switch for the daemon and the interface. It uses systemd user units on
Linux (`dofscroll.service`, `dofscroll-ui.service`), the per-user Run key on Windows and a
LaunchAgent on macOS. If the interface is not installed, only the daemon is switched. See
[installation.md](installation.md).

## Languages

English is the source language; the German catalog (`po/de.po`, `dofscroll_de.qm`) is installed
with the interface.
