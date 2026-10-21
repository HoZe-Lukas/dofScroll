<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# User interface

`dofscroll-ui` is the Qt6 tray icon and settings window, one code base for Linux, Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload. It starts minimized to the tray (no window opens); use the tray's "Settings..." to show it. Without a system tray the window is shown minimized, and closing it quits the interface (the status bar says so). With a tray, closing the window hides it to the tray (a short notice appears the first time); the tray's "Quit" ends the interface. The window remembers its size, position and last page (`ui.conf`). Checked by the maintainer only on Linux (RHEL 9, X11, KDE Plasma); the Windows and macOS builds are not run on hardware. Full text: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

This page describes the window page by page and then lists what you can do with it, task by task. The settings themselves are explained in the [Configuration reference](Configuration-reference), the joystick modes in [Modes](Modes) and [Built-in modes](Built-in-modes).

## The window

A **sidebar** on the left picks a category (Overview, Profiles, Modes, Device, Settings). Where a category has several pages, **tabs** on top of the content pick the page. The **status bar** at the bottom shows what the daemon is doing now: profile, joystick mode, navigation, paused or disabled; with several SpaceMice it lists every device with its joystick mode. Nothing has a Save button: see "Saving" below.

| Category | Page (tab) | What it is for |
|---|---|---|
| Overview | Overview | Where to start: daemon state, "Getting started" steps, task buttons, quick actions and one card per profile |
| Profiles | Profiles | Everything that depends on the device or the program: joystick mode, scroll and zoom feel, button assignment |
| Modes | Joystick modes | Built-in and your own joystick modes, with an editor |
| Modes | Button modes | Reusable behaviors for one button, with an editor |
| Modes | Extensions | Your joystick mode and button mode files in one list, each with an on/off switch |
| Device | Device | The connected devices and their button counts |
| Device | Axes | Live view of the six axes and the axis assistant |
| Settings | Settings | Output on/off, start at login, language and theme, tick rate, output backend, input path, log, About |

### Overview

- **Getting started** (until the daemon has seen a device and a profile exists): connect the SpaceMouse, check the axes with the assistant, add a profile for a program you use. Each step says "Done:" or "To do:" in words and opens the page that does it.
- **What do you want to do?** Three buttons: tune scroll and zoom, check the axes, switch joystick modes.
- **Quick actions:** add a profile, new profile for the running program, **Active joystick mode** (switches the running daemon until the next reload or restart, like the tray menu), open the log, open the documentation, release all keys, restart the daemon, restart the interface.
- **One card per profile** with its joystick mode, what it overrides and how many settings; the filter box narrows the list; "Open" jumps to the profile.

### Profiles

The list on the left holds **Global** first, then **device profiles**, then **program profiles**; each shows how many settings it overrides. The editor on the right always names the profile it edits. Tabs:

- **General:** for device and program profiles first an output switch, then the joystick mode (drop-down with an **i** button that explains in words what it does per channel). **With the output off the profile sends nothing, so the joystick mode, the buttons and the channel settings are hidden** (the Channels, Buttons and Overrides tabs too) until you switch it on again. To switch only the axes off and keep the buttons, choose the joystick mode "off" and leave the output on.
- **Channels:** sensitivity, dead zone, direction, smoothing, axis mix, curve (with a live preview), zoom detection and the step trigger, grouped per channel. A filter box hides rows that do not match. "Reset" per row or group and "Restore defaults" remove the profile's own values so the inherited ones apply again.
- **Buttons:** one row per button of the device. Each row has a drop-down: nothing, a button mode, navigation, a joystick mode switch (`mode:<id>`, next, previous), toggles, and in device and program profiles "Default (inherit from Global)".
- **Overrides:** the raw values this profile sets itself, with Add override and Remove.

**Inheritance is always written out in words:** "Inherited from Global: 5", "Set in this profile (inherited: 5)", `From joystick mode "timeline": 21`. In Global a value that other profiles override says "N profiles override this". Precedence: Global < device < joystick mode overrides < program ([Profiles](Profiles)).

Below the list: **Add application...** (a picker of the programs running now, search, "Capture next focused program", shows the exact match string that will be stored) and **Add device...** (from the connected devices).

What belongs to the selected profile is the box **This profile** at the bottom of its **General** tab: **Change application...** (or **Change device...**) picks another program or device for the profile with the same picker, so you can follow a program whose name changed and keep all settings; **Export...** (the profile as a `.conf` file) and **Remove**. Global can only be exported.

### Joystick modes

The list shows every joystick mode with its origin (**Built in**, **Your file**, **Your file, replaces the built-in joystick mode**), its description and which profiles use it. Actions: **New**, **New from example...** (the ready-made examples that are not installed yet), **Duplicate**, **Import...** (also by dropping `.conf` files on the page, checked, never overwrites), **Export...**, **Rename...**, **Edit** and **Delete**. A built-in joystick mode first gets an editable copy with the same id.

The **editor** has three tabs:
- **Settings:** name, description and the three channels (default, off, vertical or horizontal wheel, zoom, keys with a modifier chord per direction, or one key tap per push). Each scroll channel has a **Split push and tilt** switch that gives push and tilt their own action and tuning.
- **Buttons:** what each button does in this joystick mode.
- **Overrides:** settings this joystick mode changes while it is active.

Problems are listed below the form ("Not saved until this is fixed: ..."); an invalid file is never written. Comments and lines the editor does not own stay as they are. "Open in text editor" opens the file. A joystick mode file can press any key, so the page reminds you to read files from others first ([Security](Security)).

### Button modes

A **button mode** is one named behavior of a button (a key, a click, navigation, switching the joystick mode, a cycle through chosen joystick modes). The page lists them (Built in, Your file, replaces the built-in), with New, New from example, Duplicate, Import, Delete (it names what still uses it), "Create editable copy" for built-in ones and the editor for name, description and action. Choose a button mode in the drop-down of any button.

### Extensions

Your files in `modes/` and `buttons/` in one list. The **switch** turns a file off (it is renamed to `<id>.conf.disabled`, nothing is deleted; a built-in with the same id applies again) and on. Each row shows where the file is used, so you see what a switch changes. **Remove** asks first. "Open folder" opens either folder.

### Device and Axes

- **Device:** the connected SpaceMice and the number of buttons each has (this decides how many button rows a profile offers). With several devices (Windows and macOS, direct HID) every device is listed; see [FAQ](FAQ).
- **Axes:** live bars of the six axes with a marker for the peak, one cell per button, the active joystick mode and, when the joystick mode splits a scroll channel, live bars for push and tilt. **The daemon pauses its output while this page is open**, so moving the cap does not scroll the window you look at. The **axis assistant** asks you to push, tilt and twist in turn, detects which axis does what even with strong crosstalk, and applies the result to the axis mix.

### Settings

Output on/off, **Start at login** (see below), language and theme, the daemon settings (tick rate, output backend; on Windows and macOS also the **Input path**: Automatic, 3Dconnexion driver or Direct HID), the read-only daemon status, "Open config file", "Show log...", "Documentation" and **About** (version, licenses, Qt, the 3Dconnexion note).

## What you can do

| I want to ... | Where |
|---|---|
| Make scrolling faster, slower or finer | Profiles > Global (or a profile) > Channels: sensitivity, dead zone, curve |
| Change what the cap does in one program | Profiles > Add application... (pick the running program) > General: joystick mode, Channels: values |
| A program changed its name, keep the profile | Profiles > the profile > General > This profile > Change application... |
| Use another setup for one device (Pro and Compact) | Profiles > Add device... and set the joystick mode and values there |
| Switch the joystick mode quickly | Tray menu "Joystick mode", or Overview "Active joystick mode" |
| Switch the joystick mode with a button | Profiles > Buttons: "Switch to joystick mode", or a button mode like "Next joystick mode" |
| Hold a button for a temporary joystick mode (for example slow and fine) | Profiles > Buttons: hold joystick mode |
| Make a button send a key combination | Profiles > Buttons: Press key with ctrl, shift, alt or super ticked, or a button mode like "Reset zoom" (always-the-same keys belong in the vendor driver, see [Buttons](Buttons)) |
| Create my own joystick mode | Modes > Joystick modes > New, or New from example, then Edit |
| Change a built-in joystick mode | Joystick modes > Edit (creates a file with the same id); Delete brings the built-in back |
| Push and tilt do different things | Joystick mode editor > Settings > Split push and tilt |
| Share a joystick mode or button mode | Export, send the file; Import or drop a `.conf` file onto the page |
| Switch a downloaded file off without deleting it | Modes > Extensions switch |
| See what the device sends / find out which axis is which | Device > Axes, axis assistant |
| Stop a program from getting doubled input (Blender, CAD) | Profiles > Add application... and switch its output off |
| Turn dofscroll off for a moment | Tray "Enabled", or Settings > Output |
| Get rid of a stuck key | Tray "Release all keys" |
| Look at the log | Overview "Open log", Settings, or the tray |
| Start with the computer | Settings > Start at login |
| Change language or theme | Settings > Language and appearance |

## Saving

Every change is saved by itself (switches and drop-downs at once, typed text about 600 ms after you stop or when you leave the field) and the daemon reloads. The footer says "Saved" or "Could not save: ...". An invalid value is never written; the problem is shown next to the field. Closing the window and Quit write what is pending first. If the file is changed outside (a text editor, the tray), the window follows it.

## Keyboard and accessibility

The whole interface works with the keyboard: Tab order follows the layout, the sidebar moves with Up, Down, Home and End and has a focus ring, every control has an accessible name for screen readers. State is never shown by colour alone (origin, "Done:" and "To do:", errors and warnings carry words and a symbol). Targets are at least 24 x 24 px and the layout follows the system font size, tested up to 200 %. A window that is too small scrolls instead of cutting things off; at the default size no page needs a horizontal scroll bar. Contrast is at least 7:1 for text (4.5:1 for hints) in both themes.

## Several SpaceMice

Where each device arrives on its own (Windows and macOS, direct HID) the daemon keeps one lane per device: its own profile, joystick mode, holds and toggles. A button on one device switches only that device's joystick mode. One device per model (a Pro next to a Compact; a second Compact is ignored). The status bar lists every device. On Linux `spacenavd` merges the devices into one. Details: [docs/multi-device.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/multi-device.md).

## Appearance

Settings > Language and appearance: System default, Light or Dark. It applies at once and is remembered (`ui.conf`, `theme = system|light|dark`). Text keeps a contrast of at least 7:1 (4.5:1 for hints, links and the selection) in both themes. Selected rows, focus rings (2 px) and control borders are tested for contrast too, status colours are checked for colour-vision deficiencies, the interface follows the system font size, and a high-contrast request of the system is honoured (see `docs/ui.md`, section Accessibility).

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
