<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# The dofscroll interface (`dofscroll-ui`)

`dofscroll-ui` is the Qt6 interface: a tray icon plus a settings window, one code base for Linux,
Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload, so
changes apply immediately. The daemon runs without it. The interface starts minimized to the
tray (no window opens); "Open settings" or a click on the tray icon shows the window. Without a
system tray the window is shown minimized instead.

The window remembers its size, position and the page you last used (`window = x,y,w,h` and
`page = <id>` in `ui.conf`, next to the language and theme) and restores them at the next start,
also when you raise it from the tray or by starting the interface a second time. A saved position
that is off all screens (for example a monitor was unplugged) is ignored, and the size never goes
below the minimum window size. Closing the window with the close button hides it and the
interface keeps running in the tray; the first time in a run, the tray shows a short notice
("dofscroll is still running in the tray"). Without a system tray there is nothing to keep the
interface reachable, so closing the window quits it, and the status bar says so ("No system tray
found: closing the window quits dofscroll-ui"). Use "Quit" in the tray menu to quit otherwise. On Linux the older GTK tray
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
| Overview | Overview | The state of the daemon, task buttons, quick actions (see "Overview quick actions" below) and one card per device or program profile with its mode and a one-line summary of what it overrides (button bindings included); "Open" shows the profile on the Profiles page. Every profile of the configuration has a card, a filter box narrows the list |
| Profiles | Profiles | Per-device and per-program profiles, see below |
| Profiles | Channels | Scroll, zoom and the other output channels: sensitivity, dead zone, direction, curve preview; a filter box hides the rows that do not match, every row names its config key in the tooltip |
| Modes | Modes | The available modes (built-in and files in the modes folder): new, duplicate, import (button or drop `.conf` files onto the page), export, rename, edit in the page, delete; for every mode the profiles that use it (selected as its mode or switched to by a button); a reminder that mode files can press any key |
| Modes | Button modes | The button mode files (`buttons/`): create, edit in the page, duplicate, import, delete; used from the button drop-downs of the Profiles page |
| Modes | Extensions | All modes and button modes in one list with their origin (built in, your file, example not installed); add a shipped example, remove your own file. Extensions are plain files, nothing executable |
| Device | Axes | Live bars of the six axes and the axis assistant (wizard) that detects which axis does what |
| Settings | General | Output on/off, global mode, "Start at login", read-only status of the daemon, "Open config", "Show log..." and "Documentation" |

## Overview quick actions

- **What do you want to do?** Three buttons that open the page for a task: "Tune scroll and zoom"
  (Channels), "Check the axes" (Axes) and "Switch modes" (Modes).
- **Add a profile...** opens the Profiles page; **New profile for the running program...** opens the
  running-program picker of the Profiles page directly.
- **Active mode** lists the loaded modes and marks the one the daemon runs now. Picking one switches
  the running daemon to it until the next reload or restart (the same runtime request as the tray's
  Mode menu); the default mode in the configuration stays as it is. It is disabled while the daemon
  is not running.
- **Open log** (and "Show log..." on the General page and in the tray) opens a small log window: the
  end of the daemon log (the last 64 KB, at a line start; control characters are replaced), with
  Refresh and Copy. The log file is the one in the runtime directory
  (`dofscroll.log`, see [troubleshooting.md](troubleshooting.md)).
- **Documentation** (Overview, General, and on the Button modes page) opens the project
  documentation in the default browser: `docs/ui.md`, or `docs/modes.md` from the Button modes page.
  Only https addresses are ever opened.
- **Release all keys**, **Restart daemon** and **Restart interface** as before.
- The filter box above the profile cards matches every word against the name, the mode, the kind
  ("device" or "program") and the settings of a profile, ignoring ASCII case. The tooltip of a card
  lists all its settings, not only the first ones shown in the summary.
- On a very wide window the Overview and Channels pages stop at a maximum content width
  (`kMaxContentWidth` in `src/ui/model/design_tokens.hpp`) and stay centered.


## Editing a joystick mode

"Edit" on the Modes page opens an editor next to the list (a built-in mode first gets an editable
file with the same id, as before). It edits name and description, the three channels (default, off,
vertical or horizontal wheel, zoom, **keys** or **step** with modifier check boxes and a key picker
per direction), the buttons (the same drop-down per button as on the Profiles page, including the
button modes and "Default (inherit)", which removes the binding) and the settings overrides (key,
value, Add, Remove; `MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not offered). Problems are listed
inline and Save stays disabled until they are gone.

The editor has three tabs: Settings (name, description, the three channels), Buttons and
Overrides. Each tab scrolls on its own; the problem list and the buttons Save, Revert, "Open in text
editor" and "Close editor" stay visible below the tabs. The selected tab is kept while you edit
and when you open another mode. (There is no preview tab: the editor has nothing to preview.)

Save rewrites the file line by line: the lines of the keys the editor owns are updated in place, new
ones are appended, cleared ones are removed; comments, the SAFETY header and unknown lines stay as
they are, and the id (file name) never changes. The result is checked with the daemon's loader
first (a save that would drop one of its lines is refused) and replaces the file atomically; the
loader's warnings, including "can press shortcut chords", are shown below the form. If the file
changed on disk while you were editing, Save asks before overwriting. Revert reloads the file;
"Open in text editor" is still there for people who prefer the file.

Edits are never dropped silently. While the editor holds unsaved changes, selecting another mode,
Edit or a double click on the same mode (which reloads the file), New, Duplicate and Import ask
**Save / Discard / Cancel** first; Cancel keeps the list on the mode being edited. Quit in the tray
menu asks the same once for the Modes and Button modes pages. The General page (language, theme,
start at login) and the Axes page (the assistant ends with Apply or Cancel) have no form with a
Save button: their changes are applied or written at once, so they have no Revert and no unsaved
state.

## Getting started and accessibility

While the daemon has not seen a device or no profile exists yet, the Overview shows "Getting
started": connect the SpaceMouse, check the axes on the Axes page (the assistant helps), add a
profile for a program you use. Each step says "Done:" or "To do:" in words and opens the page that
does it; the box disappears once the device is seen and a profile exists. The first step opens the
Axes page (it shows what the daemon sees of the device) and, while the daemon is not running, says
so. The axes step is "Done:" after the axis assistant has been applied once; that is remembered in
`ui-progress.conf` next to the configuration file (`axis_assistant_applied = 1`), not in `ui.conf`.

Every input control has an accessible name (screen readers): the `--self-test` checks all pages for
controls without one. The whole interface works with the keyboard (tab order follows the layout;
drop-downs, spin boxes and buttons are real controls), state is never shown by colour alone (origin
and "Done:"/"To do:" are written out), and the themes meet the contrast limits of
`tests/test_ui_theme.cpp`.

## Appearance

Settings > Appearance sets the theme: **System default** (the look of the platform), **Light** or
**Dark**. A change applies at once and is remembered in `ui.conf` (`theme = system|light|dark`,
next to the language). Light and Dark use the Fusion style with the dofscroll palette
(`src/ui/model/theme_colors.hpp`); System restores the style and palette the program started with.
The palettes keep a contrast of at least 7:1 for normal text and 4.5:1 for everything else
(hints, disabled text, links, selection), checked by `tests/test_ui_theme.cpp`. The left tabs have
icons (`src/ui/qt/icons/*.svg`, drawn in black and tinted with the text colour of the current
palette); spacing follows one scale (`src/ui/model/design_tokens.hpp`). The screenshots of
`dofscroll-ui --screenshots` show the light theme at three sizes and the dark theme at one.
`dofscroll-ui --screenshots <dir>` renders every page at three window sizes into PNG files (use
`QT_QPA_PLATFORM=offscreen` on a machine without a display); CI attaches them to the UI job.

While the Axes page is open the daemon pauses its output, so moving the cap does not scroll the
window you look at. The page shows as many button cells as the factory button count of the
device (see below), or the highest button it has seen when no device information is available.

## Joystick mode and button modes

The Profiles page has a "Joystick mode" dropdown per profile (global, device, application): it
chooses what the axes do. Every button row has its own dropdown with these entries, in this order:
"Default (inherit)" (device and program profiles; it removes the profile's own `BUTTON_<n>` line,
and the row shows what is inherited; in the default profile the first entry is "Nothing"), the
**button modes** by name (tooltip: the description), then the plain actions: navigation, mode
switch (`mode:<id>`, `mode:next`), toggles (`toggle:nav`, `toggle:mode:<id>`) and, behind "Advanced"
in the default profile, click and key. Choosing a button mode writes `BUTTON_<n> = buttonmode:<id>`;
the same button mode can be chosen on several buttons. A reference to a button mode that is not
installed shows as "missing: <id>" and is not rewritten. See [button-modes.md](button-modes.md).
The tray shows "Navigation mode on" while the navigation mode is latched. Precedence is
unchanged: global < device < mode overrides < application. A profile that names a mode which does
not exist is shown with a warning instead of being changed silently.

Mode library: "Export..." saves a copy of the selected mode file (a built-in mode as its template
text) to a place you choose and never overwrites without asking. "Rename..." gives a file mode a new
name; the id (and file name) follows the name. Profiles that still use the old id are listed and
confirmed first; they are not rewritten and fall back to the default mode until you change them. A
built-in mode, or a file that replaces one, cannot be renamed. The details of every mode show "Used
by": the profile sections whose `MODE` selects it or whose `BUTTON_<n>` is `mode:<id>`,
`toggle:mode:<id>` or `hold:mode:<id>`. Dropping `.conf` files onto the Modes page imports them with
the same checks, warnings and confirmation as Import; files are only read, never run.

The **Button modes** page (category Modes, next to Modes) manages the button mode files: list,
editor for name, description and action with Save and Revert, New, Duplicate, Import, Delete (it
names the profiles and mode files that still use the button mode) and "Open button modes folder".
The same Save / Discard / Cancel question appears before another button mode is selected, or before
New, Duplicate or Import replaces a form with unsaved edits.

## Profiles and the running-program picker

- Add a device profile from the list of connected devices. The list and the factory button count
  (which limits the buttons you can bind) come from the daemon's live feed; without the feed the
  page falls back to the `device=` line of the status file.
  When the device is known, the tooltip of each "Button n" label says how many buttons the device has.
- "Add application..." opens a picker of the programs that are running now: search by name, match
  string or window title, refresh, and see the exact match string that will be stored. You never
  have to type an internal name. "Capture next focused program" waits a few seconds and takes the
  program you switch to. A manual entry with validation is the fallback.
- The editor of the selected profile has sub-tabs: **General** (what the profile is, the joystick
  mode, and for another mode its read-only channels and buttons), **Channels** (the effective
  values, see below), **Buttons** (bindings, navigation and click button; not shown for a profile
  whose mode has its own bindings) and **Overrides** (values only this profile sets; "Remove" on an
  override makes the profile inherit the value again). The default mode has General and Buttons
  only. Each tab scrolls on its own; the status line, Revert and Save stay visible below the tabs.
  The last tab you used is kept for the session (a profile without that tab shows General).
- Revert drops the unsaved edits of the selected profile and loads the saved values (a profile you
  added since the last save disappears, a rename is undone); other profiles keep their edits and
  "Save" still writes everything. When you select another profile, another page or close the
  window while the shown profile has unsaved edits, a question offers Save, Discard or Cancel
  (closing asks about any unsaved edit).
- Search: the box above the profile list shows only the programs and devices whose name contains
  the text; the box above the tabs shows only the settings of the selected profile whose label or
  key matches (button rows by "Button n", `BUTTON_n` or the action). Matching ignores ASCII case.
- "Effective values" (device and program profiles) lists every channel setting with the value that
  applies and where it comes from, written out: built-in default, global settings, this device
  profile, the mode, or this program profile (precedence as above). "Reset" removes the value the
  profile sets itself.
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
`src/ui/model/restart_plan.hpp`. On Windows the helper programs start with `-NoProfile
-NonInteractive -WindowStyle Hidden` and `CREATE_NO_WINDOW`, so no console window flashes up, and a
failed stop of the daemon is reported instead of starting a second one. Untested on real desktops
for Windows and macOS.

## Platform notes (Windows and macOS)

Compiled and unit-tested in CI only; what has to be checked by hand on real machines:

- **Windows console output.** `dofscroll-ui.exe` is a GUI program without a console. For `--help`,
  `--version`, `--self-test`, `--screenshots` and the "already running" message it attaches to the
  console of the parent (cmd, PowerShell) and writes there, unless the output is redirected to a
  file or pipe. Started by double click there is no console and no output. To check: run
  `dofscroll-ui.exe --version` in PowerShell and in cmd, also with `| more` and `> out.txt`.
- **macOS menu bar icon.** The tray uses a monochrome template icon
  (`icons/dofscroll-tray-template.svg`, `QIcon::setIsMask`) that macOS colors for light and dark
  menu bars; the "disabled" and "not running" states are the same icon with less opacity. macOS does
  not deliver a click on the icon when the icon has a menu, so there is no "click to open the
  window": use the **Settings...** entry of the menu.
- **macOS Dock.** The `.app` has `LSUIElement` (no Dock icon). The same Info.plist is embedded in
  the plain executable of a CMake build (linker option `-sectcreate`), so a run from the build
  directory does not show a Dock icon without a window either. Unchecked on hardware.
- **Output backend.** The "Output backend" choice (X11, uinput) on the Channels page exists on
  Linux only; Windows uses SendInput and macOS CGEvent.
- **German catalog.** `dofscroll_de.qm` is next to the executable on every system (Linux
  `/usr/bin`, Windows install folder, macOS `dofscroll.app/Contents/MacOS`). To check: set the
  language to German on the General page and restart the interface.

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
