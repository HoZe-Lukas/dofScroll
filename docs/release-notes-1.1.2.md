<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.1.2

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Release after [1.1.1](release-notes-1.1.1.md), mostly about the interface: you now see at a glance what a joystick
mode does, the pages look and read the same, and the built-in modes explain themselves. The daemon, the command
line and the configuration format do not change.

## What changed

### What each joystick mode does, at a glance (#161)

- **The six motions.** Every joystick mode is described by what each motion of the cap does: vertical push, vertical
  tilt, horizontal push, horizontal tilt, twist, and lift / press (push down or pull up from the top, used only through a
  channel mix, so the built-in modes say "Not used"). The Joystick modes list shows six symbols per mode in a fixed
  order (a dashed symbol means off or not used); the detail view shows six cards with the words and how the cap moves.
- **One look everywhere.** The same component shows the assignment on the Joystick modes page, in the profile's General
  tab ("What this profile does": the effective assignment, with where each value comes from: this profile, the
  joystick mode, or inherited), on the Overview cards and in the info popover. Buttons appear as numbered chips such as
  "1 Space" or "15 Navigation", on the Button modes page and next to every drop-down on the Buttons tab. A muted or
  dashed chip always carries its word, never colour alone.
- View only: the file format, the loader and the daemon are unchanged.

### Clearer built-in modes (#163, #157)

- **Descriptions in plain words** for all 14 built-in joystick modes and the 5 built-in button modes: what the mode
  does and for which program it is meant (for example "Navigation makes the cap work like arrow keys: push or tilt
  presses Up, Down, Left or Right instead of scrolling ..."). A test keeps every description between 60 and 256 bytes.
- **The docs and the wiki describe every built-in joystick mode by its six motions**, in one table, and a test
  (`tests/test_docs_motions.sh`) fails when the table no longer matches the mode files. Two wrong statements were
  corrected (the built-in `timeline` has no button bindings; `fast` is built in).

### Choose what Next and Previous joystick mode go through (#165)

On the Button modes page, the actions Next and Previous joystick mode have a "Cycle through" section: all joystick
modes (alphabetical) or only the ticked ones, in a numbered order with Move up and Move down. A mode that is in the
file but not loaded is shown as missing and kept. Before, this list (`CYCLE`) could only be edited in the file.
Fixed on the way: an editable copy of a built-in button mode lost its cycle list.

### Profiles (#153)

The output switch is the first box of a profile's General tab. With the output off, a profile sends nothing, so
the joystick mode, the buttons and the channel settings are hidden (the Channels, Buttons and Overrides tabs too)
until you switch it on again. To switch only the axes off and keep the buttons, choose the joystick mode "off" and leave the
output on (now covered by a test).

### User modes (#155, #159)

- The page that lists your joystick mode and button mode files is called **User modes** (it was "Extensions").
- Files that an older version copied into your modes folder on its first start are recognised: all historical texts
  of those examples are known now (several per mode), the loader ignores each of them, and the page marks such a file
  as a copy of a shipped example that is not used. Before, an old `timeline.conf` replaced the built-in `timeline`
  with button bindings you never wrote. A copy you edited still counts as your own file.

### Website and wiki

New screenshots of the interface (English and German), the wiki page "User interface" describes every page and what
you can do with it, task by task, and the website has one download button instead of three that led to the same page.

## Not verified

The interface changes were built and self-tested offscreen on Linux with Qt 6.9.3 and by CI on Windows and macOS; they
were not looked at on a real desktop. The look (symbols in the real fonts of Windows and macOS, wrapping, sizes) needs a
human on each system. Several SpaceMice, the tray after a real KDE login and the new modes in real programs are still
unverified on hardware (see the open issues).
