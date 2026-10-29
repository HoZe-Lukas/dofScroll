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

Shared by all lanes: the configuration (`dofscroll.conf` and the profile files in `devices/` and `apps/`), the joystick mode and button mode files, the active program
(`[app:*]` matches the same program for every device), the output backend and the status file.

The output counts who holds a key or a mouse button: Ctrl for zoom is pressed when the first device needs
it and released when the last one lets go. Releasing everything of one device (unplug, switching the
joystick mode, `ENABLED = 0`) never releases a key another device still holds. Wheel steps and key taps pass
straight through.

The tray's "Joystick mode" menu, "Release all keys" and the Pause of the axis display act on all devices.
A joystick mode requested through the tray applies to every lane.

## Addressing a device in profiles

`[device:<text>]` (the header of a file in `devices/`, see [configuration.md](configuration.md#profile-files)) matches when the device name contains `<text>` (case-insensitive). With one device, or
devices with different names, nothing changes: `[device:SpaceMouse Pro]` and
`[device:SpaceMouse Compact]` address one device each.

**One device per model.** A Pro next to a Compact works; two of the same model do not: the second device with the
same name is ignored (one log line, `only one device per model is supported`), because `[device:*]` could not tell
them apart and the numbering would change with the order the devices are found. At most 4 devices are used.

## Hotplug

- Plugging in a device creates its lane (starts in the joystick mode its profile gives).
- Unplugging a device ends its lane and releases only what that lane held. The other lanes keep their joystick
  mode, holds and toggles.
- A device that comes back under a new id (replug) takes over the lane of the same model, so a joystick mode selected by a button survives a replug of a single device.
- The last lane always exists. With a merged source (Linux) an unplug clears the state of the merged
  stream, because the source cannot say which physical device went away.

## Status and interface

The status file keeps its plain keys for the first device and adds, with several devices,
`lanes=<n>`, `active_device=<name>` and `device<k>=`, `profile<k>=`, `mode<k>=`, `toggle<k>=`, `enabled<k>=` for
the devices k = 2..n (`src/common.hpp`). The interface then shows "Devices: A (Joystick mode: normal), B
(Joystick mode: timeline)" in the status line and drops the warning "only A is reported". The Axes page and the
live feed show the device that moved last. A sample source frame (type 3, no version bump) names the device of each
sample, so the status line names it and a "Device" selector on the Axes page and in the axis assistant (shown with
two or more devices) pins both to one device; "All devices" follows the one that moved last.

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
