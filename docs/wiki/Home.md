<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll

dofscroll turns a 6DoF 3D mouse such as the 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys and mouse buttons for ordinary desktop programs. On Linux it reads the device through `spacenavd`/`libspnav` and sends the events through X11 (XTest) or, for Wayland, through uinput. The Windows and macOS ports read the device through raw HID.

**Status: pre-release (0.1 candidates).** Only one setup has been verified on real hardware. Everything else is built and unit-tested in CI but has not been run on a device.

## Tested on

| Platform / device | Status |
|---|---|
| RHEL 9, X11, KDE Plasma | **Tested** with SpaceMouse Pro and SpaceMouse Compact |
| Other Linux distributions (Debian, Ubuntu, Fedora, ...) | Not tested; RPM, DEB and TGZ are built by CI but not installed and run there |
| Linux, Wayland | Not tested (uinput backend, opt-in udev rule) |
| Windows | Implemented and CI-built; **not run on hardware**; no installer, no tray |
| macOS | Implemented and CI-built; **not run on hardware**; no installer, no tray |
| SpaceMouse Wireless, Pro Wireless, Enterprise, SpaceNavigator | Not tested |

## Programs

| Program | Task |
|---|---|
| `dofscroll` | Background daemon: reads the device, produces the input events |
| `dofscroll-tray` | Linux GTK tray icon with a settings window; writes the same config file and tells the daemon to reload (optional) |

A Qt6 interface for all three systems is planned, see [Roadmap](Roadmap).

## Where to start

1. [Installation](Installation), then [Quick start](Quick-start)
2. Tune it: [Configuration reference](Configuration-reference), [Profiles](Profiles), [Buttons](Buttons)
3. Change what the device does: [Modes](Modes)
4. Something wrong: [Troubleshooting](Troubleshooting), [FAQ](FAQ)
5. Contribute: [Development](Development), [Architecture](Architecture)

Other pages: [Devices](Devices), [Security](Security), [Roadmap](Roadmap), [Release notes](Release-notes).

## Links

- Repository: https://github.com/HoZe-Lukas/dofScroll
- Releases: https://github.com/HoZe-Lukas/dofScroll/releases
- Full docs in the repository: [readme.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/readme.md), [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md)
- License: [GPL-3.0-or-later](https://github.com/HoZe-Lukas/dofScroll/blob/main/LICENSE); third-party licenses: [THIRD-PARTY-NOTICES.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/THIRD-PARTY-NOTICES.md)
