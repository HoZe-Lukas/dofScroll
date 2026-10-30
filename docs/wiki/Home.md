<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll

dofscroll turns a 6DoF 3D mouse such as the 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys for ordinary desktop programs. On Linux it reads the device through `spacenavd`/`libspnav` and sends the events through X11 (XTest) or, for Wayland, through uinput. The Windows and macOS ports read the device through the installed 3Dconnexion driver's interface when the driver is installed, and through raw HID otherwise (setting `INPUT`).

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. See [Buttons](Buttons).

**Status: version 1.2.3.** Tested extensively only on RHEL 9 (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux distributions were install-tested in CI containers (a contributor reports that 1.2.3 starts and shows its window on Ubuntu 26.04, GNOME, Wayland, SpaceMouse Compact; not tested by the maintainer); on Windows the basic functions work on one real machine (SpaceMouse Pro, official 3Dconnexion driver; first tests, not extensive); the macOS build is built and unit-tested by CI but has never been run with a real device.

## Tested on

| Platform / device | Status |
|---|---|
| RHEL 9, X11, KDE Plasma | **Tested** with SpaceMouse Pro and SpaceMouse Compact |
| Other Linux distributions (Debian, Ubuntu, Fedora, ...) | Not tested with a device; RPM, DEB and TGZ are built by CI and install-tested in containers (Fedora 42, Debian 12 and 13, Ubuntu 24.04) |
| Linux, Wayland | Not tested by the maintainer (uinput backend, opt-in udev rule) |
| Ubuntu 26.04, GNOME, Wayland, SpaceMouse Compact, uinput backend | **Reported by a contributor, not tested by the maintainer:** after the 1.2.3 fixes the services start and the settings window renders |
| Windows | Daemon, Qt interface and unsigned installer/ZIP built by CI; **basic functions work on one real machine** (SpaceMouse Pro, official driver; first tests) |
| macOS | Daemon, Qt interface and unsigned, not notarized DMG built by CI; **not run on hardware** |
| SpaceMouse Wireless, Pro Wireless, Enterprise, SpaceNavigator | Not tested |

## New in 1.3.0

- **One file per profile** (`devices/`, `apps/`) with an automatic, backed-up migration, Import and Export, and an order for overlapping profiles ([Profiles](Profiles)).
- **Every motion can do something in a profile**, the lift is the sixth motion, **Take over** copies a motion or a button from another profile, **New profile** builds one from parts.
- **Keys on every motion, analog or digital**, and **mouse clicks and holds**; a key picker as a tree in columns ([Joystick modes](Modes)).
- A reworked axis assistant, a device selector on the Axes page, and a setup guide that changes nothing until you press Finish.
- **Macros** (`macro:`, key down, key up and pauses, once per push or button press) and the **New function ...** window that composes a key, a click, a hold, scroll or zoom, the pointer or a recorded macro ([Profiles](Profiles#new-function-and-macros)).
- **The pointer as a motion function** (`pointer_x`, `pointer_y`, `MOUSE_SPEED`) and ready tiles "Switch tabs", "Volume" and "Change track" in the joystick mode editor.
- Hardened parsers and raised size limits (1 MiB per profile file, 16 MiB together per load). **Not verified:** nothing was tested with a SpaceMouse in hand; see the [release notes](Release-notes).

## Highlight: no doubled input next to the official driver (Windows)

If the official 3Dconnexion driver also scrolls in a program, that program gets every movement twice. **Settings > General > Remove driver axis settings...** lists the programs with active driver axes; you tick the ones to switch off (and leave your CAD and 3D programs alone), every changed file is saved first and can be restored. See [User interface](User-interface#driver-cleanup-windows).

## Programs

| Program | Task |
|---|---|
| `dofscroll` | Background daemon: reads the device, produces the input events |
| `dofscroll-ui` | Qt6 tray icon and settings window for Linux, Windows and macOS, see [User interface](User-interface) (part of every package) |

## Where to start

1. [Requirements](Requirements) (supported systems, drivers, permissions), [Installation](Installation), then [Quick start](Quick-start)
2. Tune it: [Configuration reference](Configuration-reference), [Profiles](Profiles), [Buttons](Buttons)
3. Change what the device does: [Joystick modes](Modes), the list of [Built-in joystick modes](Built-in-modes), the tutorial [How to write a joystick mode](Writing-modes)
4. Something wrong: [Troubleshooting](Troubleshooting), [FAQ](FAQ)
5. Contribute: [Development](Development), [Architecture](Architecture), [Joystick mode requests and contributions](Contributing-modes)

Known limitations are listed in the [Release notes](Release-notes).

Other pages: [Requirements](Requirements), [User interface](User-interface), [Devices](Devices), [Security](Security), [Roadmap](Roadmap), [Release notes](Release-notes).

## Links

- Repository: https://github.com/HoZe-Lukas/dofScroll
- Releases: https://github.com/HoZe-Lukas/dofScroll/releases
- Full docs in the repository: [readme.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/readme.md), [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md)
- License: [GPL-3.0-or-later](https://github.com/HoZe-Lukas/dofScroll/blob/main/LICENSE); third-party licenses: [THIRD-PARTY-NOTICES.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/THIRD-PARTY-NOTICES.md)
