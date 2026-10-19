<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll

dofscroll turns a 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys and mouse
buttons for ordinary desktop programs. It reads the device through `spacenavd`/`libspnav` and
sends the events through X11 (XTest) or, for Wayland, through uinput.

It consists of two programs:

| Program | Task |
|---|---|
| `dofscroll` | Background daemon. Reads the SpaceMouse, produces the input events. |
| `dofscroll-tray` | Tray icon with a settings window. Writes the same config file and tells the daemon to reload. Optional. |

## Status and testing

Everything below was verified by the maintainer on real hardware unless it says otherwise.
"Not tested" means the code builds (and the unit tests pass in CI) but it has not been run there.

| Platform | Status |
|---|---|
| RHEL 9, X11, KDE Plasma | **Tested**, with the SpaceMouse Pro and the SpaceMouse Compact. |
| Other Linux distributions (Debian, Ubuntu, Fedora, ...) | Not tested. The RPM, DEB and TGZ packages are built by CI but have not been installed and run on those systems. |
| Linux, Wayland | Not tested. Uses the uinput backend and needs a udev rule; active-window profiles work only with `kdotool` (KDE). |
| Windows | In development, not usable yet. See [docs/work-plan.md](docs/work-plan.md). |
| macOS | In development, not usable yet. See [docs/work-plan.md](docs/work-plan.md). |

| Device | Status |
|---|---|
| SpaceMouse Pro | Tested (RHEL 9, X11, KDE Plasma). |
| SpaceMouse Compact | Tested (RHEL 9, X11, KDE Plasma). |
| SpaceMouse Wireless, Pro Wireless, Enterprise, SpaceNavigator and other models | Not tested. The factory button count comes from vendor information, see [docs/devices.md](docs/devices.md). |

Supported devices: models that `spacenavd` supports. The factory button count is known for
SpaceMouse Pro, Pro Wireless, Compact, Wireless, Enterprise and SpaceNavigator. Automated tests
(CI) run on Rocky Linux 9 only; the Windows and macOS runners are added with the ports.

## Download and install

1. Open the [Releases page](https://github.com/HoZe-Lukas/dofScroll/releases)
   (newest stable: [/releases/latest](https://github.com/HoZe-Lukas/dofScroll/releases/latest)).
2. Download the package for your system plus `SHA256SUMS.txt`.
3. Verify: `sha256sum -c --ignore-missing SHA256SUMS.txt`
4. Install: `sudo dnf install ./dofscroll-*.rpm` (RHEL family, needs EPEL) or
   `sudo apt install ./dofscroll_*.deb` (Debian family).
5. Follow the first-time setup in [docs/installation.md](docs/installation.md). The user units
   are not installed where systemd looks for them on every distribution, so do not skip it.

## First run

1. `sudo systemctl enable --now spacenavd`
2. Enable the user services (after the setup step in the installation guide):
   `systemctl --user enable --now dofscroll.service dofscroll-tray.service`
3. Plug in the SpaceMouse. Push the cap: the window under the pointer scrolls. Twist: it zooms
   (Ctrl + scroll).
4. Right-click the tray icon, open "Settings" and adjust sensitivity, dead zone and direction.
   Changes apply immediately.

If nothing happens, see [docs/troubleshooting.md](docs/troubleshooting.md).

## Documentation

| Document | Content |
|---|---|
| [docs/installation.md](docs/installation.md) | Install from a release, first-time setup, upgrade, uninstall, build from source |
| [docs/configuration.md](docs/configuration.md) | Config file, all keys, profiles, precedence, examples |
| [docs/modes.md](docs/modes.md) | Modes, button actions, key names |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Common problems, logs, status files |
| [docs/devices.md](docs/devices.md) | Device descriptions |
| [docs/security.md](docs/security.md) | Threat model, systemd hardening, uinput permissions |
| [docs/releasing.md](docs/releasing.md) | How a release is made |
| [docs/work-plan.md](docs/work-plan.md) | Roadmap and the Windows/macOS ports |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build, test, pull requests, adding a mode or language |

## Architecture in short

- A reader thread stores the newest raw axis values. The main thread runs a fixed tick
  (`TICK_HZ`, default 250 Hz) and turns them into events, so scrolling is even and does not
  depend on the driver's event rate.
- The daemon reloads its config on `SIGHUP` and when the file changes. It reconnects to
  `spacenavd` by itself. It never needs root.
- The source code in `src/` is the reference for behavior. The headers are the contracts.

## License and trademarks

dofscroll is licensed under the GNU General Public License, version 3 or (at your option) any
later version (SPDX: `GPL-3.0-or-later`), see [LICENSE](LICENSE). Licenses of the libraries it
builds on are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

"3Dconnexion" and "SpaceMouse" are trademarks of 3Dconnexion GmbH or their respective owners.
dofscroll is an independent project and is neither endorsed by nor affiliated with 3Dconnexion.
The names are used descriptively only, to name the supported hardware.
