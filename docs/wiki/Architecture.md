<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Architecture

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

One shared core and a thin platform layer per OS. The source code is the reference for behavior and the headers are the contracts. Full text: [docs/port-architecture.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-architecture.md).

## Layers

```
 +------------------------------------------------------------+
 | UI: dofscroll-ui (Qt6: tray icon and settings window)      |
 +----------------------+-------------------------------------+
                        | writes config, asks for reload
 +----------------------v-------------------------------------+
 | Daemon core (src/daemon_core.*)                            |
 |   config, dsp, modes + mode_runner, profile_match, seed    |
 +----------+-------------------+-----------------+-----------+
            | InputSource       | Output          | ActiveWindow / paths / reload
 +----------v------+   +--------v--------+   +----v-----------------+
 | Linux: spnav    |   | X11 XTest       |   | X11 window class     |
 | Windows: raw HID|   | uinput (Linux)  |   | XDG paths, SIGHUP    |
 | macOS: IOHID    |   | SendInput (Win) |   | Win: foreground exe, |
 |                 |   | CGEvent (mac)   |   |   named event        |
 +-----------------+   +-----------------+   | mac: NSWorkspace     |
                                             +----------------------+
```

- **Daemon core** ([src/daemon_core.*](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/daemon_core.cpp)): platform independent. A reader thread stores the newest raw axis values; the main thread runs a fixed tick (`TICK_HZ`, default 250 Hz) and turns them into events, so scrolling is even and independent of the driver's event rate. Reloads on `SIGHUP` (Linux) and when files change; reconnects to `spacenavd` by itself.
- **Platform layer** ([src/platform/platform.hpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/platform/platform.hpp)): interfaces `InputSource`, `ActiveWindowProvider`, paths, `ReloadListener`. Implementations live in `src/platform/linux/`, `win/` and `mac/`. A scripted `FakeInputSource` makes the core testable without hardware.
- **File layer** (`src/platform/fs_posix.cpp`, `fs_win.cpp`, `path_rules.hpp`): bounded, symlink-aware reads and atomic writes with private permissions, one implementation per OS family.
- **Pure logic** (unit-tested everywhere): `config`, `dsp`, `modes`, `mode_runner`, `profile_match`, `device_db`, `hid_descriptor`, `key_tables`, `seed`.

## Data flow

```
device -> InputSource -> latest axes (reader thread)
                              |
        tick (250 Hz): per channel mix, dead zone, curve, smoothing,
                       sensitivity, sign (src/dsp.*)
                              |
        mode + profile decide: wheel / zoom (Ctrl+wheel) / keys / none
                              |
                         Output -> X11 / uinput / SendInput / CGEvent
buttons -> BUTTON_<n> action (click, key, nav, mode:...) -> Output
config / modes files change or SIGHUP -> reload -> new settings
active window class -> [app:*] profile -> resolved settings
```

## Why direct HID on the ports

`spacenavd` exposes all SpaceMice as one merged stream, so per-device profiles and button counts are impossible through it. With direct HID access each device is its own handle with its own ID and report stream. The vendor SDK is not shipped, linked or used at build time on any OS (proprietary, revocable license, incompatible with distributing a GPL-3.0-or-later program; a project decision, not legal advice). On Windows and macOS the installed official driver's interface is loaded at run time as a second input path (`INPUT`); nothing of it is contained in dofscroll.

Port details: [port-windows.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows.md), [port-macos.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos.md). See also [Development](Development) and [Roadmap](Roadmap).
