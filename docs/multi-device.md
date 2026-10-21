<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Several SpaceMice on one computer

Design and behavior for two or more SpaceMice at once (issue #93). What works depends on how the
platform delivers the devices.

| Platform / input path | Devices arrive as | Result |
|---|---|---|
| Linux (`spacenavd`) | one merged stream (device id 0) | one device for dofscroll; `[device:*]` applies to all |
| Windows and macOS, direct HID (`INPUT = hid` or `auto` without the official driver) | one stream per device | **one lane per device** |
| Windows and macOS, official driver interface (`INPUT = driver`) | the driver's single stream | one device |

The one-lane-per-device behavior is covered by tests with a fake source with two devices
(`tests/test_multi_device.cpp`). It has not been run with two real devices on any system (issue #93 keeps
the hardware test open).

## Lanes

The daemon keeps one **lane** per attached device. A lane is everything that belongs to one device:

- the resolved profile (`[global]` < `[device:*]` < joystick mode overrides < `[app:*]`), so a device
  profile applies to the right device only,
- the active joystick mode: a button on device B switches the joystick mode of device B only,
  `hold:`, `toggle:` and the navigation latch are per device,
- the DSP state of the axes and the stale watchdog (a deflection that stops arriving ends only for the
  device that stopped),
- the safety net against stuck keys.

Shared by all lanes: the config file, the joystick mode and button mode files, the active program
(`[app:*]` matches the same program for every device), the output backend and the status file.

The output counts who holds a key or a mouse button: Ctrl for zoom is pressed when the first device needs
it and released when the last one lets go. Releasing everything of one device (unplug, switching the
joystick mode, `ENABLED = 0`) never releases a key another device still holds. Wheel steps and key taps pass
straight through.

The tray's "Joystick mode" menu, "Release all keys" and the Pause of the axis display act on all devices.
A joystick mode requested through the tray applies to every lane.

## Addressing a device in profiles

`[device:<text>]` matches when the device name contains `<text>` (case-insensitive). With one device, or
devices with different names, nothing changes: `[device:SpaceMouse Pro]` and
`[device:SpaceMouse Compact]` address one device each.

When **identical models** are attached, their names would be the same, so the daemon appends a suffix to
the name each of them is matched with: `SpaceMouse Compact #1`, `SpaceMouse Compact #2`. The number counts
in the order the devices were found. If the platform source knows a serial number, it is used instead of the
number (`SpaceMouse Compact #<serial>`). `[device:SpaceMouse Compact]` still matches both;
`[device:SpaceMouse Compact #2]` only the second. The suffixed names are what the interface lists (Axes page,
Add device dialog, status line).

The serial number is currently not filled in by the Windows and macOS sources, so the number counts
in the order the devices were found: after a reboot or another USB port the numbers can swap. Use the
name suffix only when you can live with that; for two different models use the model names.

## Hotplug

- Plugging in a device creates its lane (starts in the joystick mode its profile gives).
- Unplugging a device ends its lane and releases only what that lane held. The other lanes keep their joystick
  mode, holds and toggles.
- A device that comes back under a new id (replug) takes over the lane of the same model if its old
  lane was waiting, so a joystick mode selected by a button survives a replug of a single device.
- The last lane always exists. With a merged source (Linux) an unplug clears the state of the merged
  stream, because the source cannot say which physical device went away.
- At most 4 devices are used; more are ignored with a log line.

## Status and interface

The status file keeps its plain keys for the first device and adds, with several devices,
`lanes=<n>`, `active_device=<name>` and `device<k>=`, `profile<k>=`, `mode<k>=`, `toggle<k>=`, `enabled<k>=` for
the devices k = 2..n (`src/common.hpp`). The interface then shows "Devices: A (Joystick mode: normal), B
(Joystick mode: timeline)" in the status line and drops the warning "only A is reported". The Axes page and the
live feed show the device that moved last; the live feed protocol did not change, so the page lists all device
names and cannot say which one a sample came from (a device selector is not built).

## Linux: the decision

`spacenavd` merges all devices before dofscroll sees an event, and its protocol (`libspnav`) has no device id.
Per-device events on Linux would mean reading the devices next to `spacenavd` (hidraw or evdev), which
depends on whether the daemon and the kernel let a second reader open the same device, and needs udev
permissions for the hidraw nodes. Neither was evaluated on hardware, so **this release keeps the merged
stream on Linux**: one lane, `[device:*]` applies to all devices, buttons of the devices share the indices,
and the daemon logs a one-time note when it has seen two devices. A per-device path on Linux is open work.

## What only a human can verify

Two real devices on Windows and macOS (direct HID): lanes appear, independent joystick modes, Ctrl for zoom
on both, unplug of one, replug. See `docs/port-windows-checklist.md` and `docs/port-macos-checklist.md`
(issues #36, #37). Two devices on Linux behave as one.
