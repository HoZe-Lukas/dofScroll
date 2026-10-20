<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll

dofscroll turns a 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys and mouse
buttons for ordinary desktop programs. It reads the device through `spacenavd`/`libspnav` and
sends the events through X11 (XTest) or, for Wayland, through uinput.

It consists of two programs. The Qt interface is the only tray and settings window on Linux, Windows and macOS (the old GTK tray `dofscroll-tray` was removed in 0.1.0-beta.3, #43):

| Program | Task |
|---|---|
| `dofscroll` | Background daemon. Reads the SpaceMouse, produces the input events. |
| `dofscroll-ui` | Qt6 tray icon and settings window for Linux, Windows and macOS (pages Channels, General, Axes, Modes, Profiles). Writes the same config file and tells the daemon to reload. See [docs/ui.md](docs/ui.md). |

## Design principle

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. See [docs/modes.md](docs/modes.md#buttons-and-the-vendor-driver).

## Status and testing

This is the **0.1.0-beta.3** state of `main`. Only one setup was verified by the maintainer on
real hardware; everything else is built and unit-tested by CI but has not been run on a device.

| Platform | Status |
|---|---|
| RHEL 9, X11, KDE Plasma | **Tested** with the SpaceMouse Pro and the SpaceMouse Compact. |
| Other Linux distributions (Debian, Ubuntu, Fedora, ...) | Not tested. The RPM, DEB and TGZ packages are built by CI but have not been installed and run there. |
| Linux, Wayland | Not tested. Uses the uinput backend and needs a udev rule that you install yourself (opt-in, see [installation.md](docs/installation.md)); active-window profiles work only with `kdotool` (KDE). |
| Windows | Daemon, Qt interface and an unsigned installer/ZIP are built by CI. **Not run on hardware.** |
| macOS | Daemon, Qt interface and an unsigned, not notarized DMG (Apple silicon) are built by CI. **Not run on hardware.** |

| Device | Status |
|---|---|
| SpaceMouse Pro | Tested (RHEL 9, X11, KDE Plasma). |
| SpaceMouse Compact | Tested (RHEL 9, X11, KDE Plasma). |
| SpaceMouse Wireless, Pro Wireless, Enterprise, SpaceNavigator and other models | Not tested. The factory button count comes from vendor information, see [docs/devices.md](docs/devices.md). |

Automated tests (CI) run on Linux, Windows and macOS runners; they cover logic, not real devices.

### Known limitations

- Windows and macOS: never run with a real device. The Windows installer is not code-signed
  (SmartScreen warns) and the macOS DMG is only ad-hoc signed and not notarized (Gatekeeper
  blocks the first start). Notes in [docs/installation.md](docs/installation.md).
- The Qt interface has not been checked on Windows or macOS desktops.
- Pausing output while the own session is not the active one (several users on one PC) was
  unit-tested with fake probes only; no second user session was available. The Windows and macOS
  probes are compile-checked only.
- On Linux `spacenavd` reports one merged device, so two mice with different modes at once are
  not possible.
- Wayland: not tested; the uinput udev rule is opt-in; active-window profiles need `kdotool`.
- Supported devices: models that `spacenavd` supports (Linux) or that report a known HID layout
  (Windows, macOS); the factory button count is known for SpaceMouse Pro, Pro Wireless, Compact,
  Wireless, Enterprise and SpaceNavigator.

Release notes: [docs/release-notes-0.1.0-beta.3.md](docs/release-notes-0.1.0-beta.3.md) (and [beta.2](docs/release-notes-0.1.0-beta.2.md), [beta.1](docs/release-notes-0.1.0-beta.1.md)).

## Download and install

1. Open the [Releases page](https://github.com/HoZe-Lukas/dofScroll/releases)
   (newest stable: [/releases/latest](https://github.com/HoZe-Lukas/dofScroll/releases/latest)).
2. Download the package for your system plus `SHA256SUMS.txt`.
3. Verify: `sha256sum -c --ignore-missing SHA256SUMS.txt`
4. Install: `sudo dnf install ./dofscroll-*.rpm` (RHEL family, needs EPEL) or
   `sudo apt install ./dofscroll_*.deb` (Debian family). Linux packages contain the Qt interface
   (`dofscroll-ui`) when Qt6 was available at build time.
   Windows: unsigned `-setup.exe` or portable ZIP (SmartScreen: More info, Run anyway). macOS:
   unsigned DMG (right-click > Open). Details per system: [docs/installation.md](docs/installation.md).
5. Follow the first-time setup in [docs/installation.md](docs/installation.md). The user units
   are not installed where systemd looks for them on every distribution, so do not skip it.

## First run

1. `sudo systemctl enable --now spacenavd`
2. Enable the user services (after the setup step in the installation guide):
   `systemctl --user enable --now dofscroll.service dofscroll-ui.service`
   "Start at login" in the interface switches autostart on and off.
3. Plug in the SpaceMouse. Push the cap: the window under the pointer scrolls. Twist: it zooms
   (Ctrl + scroll).
4. Right-click the tray icon, open "Settings..." and adjust sensitivity, dead zone and direction.
   Changes apply immediately.

If nothing happens, see [docs/troubleshooting.md](docs/troubleshooting.md).

## Documentation

| Document | Content |
|---|---|
| [docs/installation.md](docs/installation.md) | Install from a release, first-time setup, upgrade, uninstall, build from source |
| [docs/ui.md](docs/ui.md) | The Qt interface: pages, mode dropdown, profiles, running-program picker, tray |
| [docs/configuration.md](docs/configuration.md) | Config file, all keys, profiles, precedence, examples |
| [docs/button-modes.md](docs/button-modes.md) | Button modes: named button behaviors in `buttons/*.conf`, chosen per button |
| [docs/modes.md](docs/modes.md) | Modes, button actions, key names, buttons versus the vendor driver (always-the-same button mappings belong in the driver) |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Common problems, logs, status files |
| [docs/devices.md](docs/devices.md) | Device descriptions |
| [docs/security.md](docs/security.md) | Threat model, systemd hardening, uinput permissions |
| [docs/releasing.md](docs/releasing.md) | How a release is made |
| [docs/release-notes-0.1.0-beta.3.md](docs/release-notes-0.1.0-beta.3.md) | Release notes of 0.1.0-beta.3; beta.2 in [docs/release-notes-0.1.0-beta.2.md](docs/release-notes-0.1.0-beta.2.md), beta.1 notes in [docs/release-notes-0.1.0-beta.1.md](docs/release-notes-0.1.0-beta.1.md) |
| [docs/work-plan.md](docs/work-plan.md) | Roadmap and the Windows/macOS ports |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build, test, pull requests, adding a mode or language |

## Text and encoding

All text is UTF-8: configuration, mode and device files, logs, and every path (spaces, umlauts,
CJK, emoji and characters such as `#` `;` `=` `%` `&` `[` `]` are fine). A BOM is accepted when
reading and never written; case-insensitive matching folds ASCII letters only. Details and limits:
[docs/encoding.md](docs/encoding.md).

## Security notes

- **Mode files are code-like.** A mode file can press any key from the key list (including Ctrl,
  Alt and Super, and chords such as Ctrl+W through the zoom channel) while a button is held or an
  axis is deflected. Only use mode files from sources you trust and read them first: look for
  `key:`, `keys:`, `zoom` and `mode:`. The daemon warns at load time about modifier-capable
  bindings. Details in [docs/modes.md](docs/modes.md#safety-and-sharing-modes). `ENABLED`,
  `BACKEND` and `TICK_HZ` are not allowed in mode files.
- **The uinput udev rule is opt-in.** Packages do not enable `/dev/uinput` access. Only Wayland
  users need it; see [docs/installation.md](docs/installation.md).
- Threat model, hardening and open points: [docs/security.md](docs/security.md).

## Architecture in short

- A reader thread stores the newest raw axis values. The main thread runs a fixed tick
  (`TICK_HZ`, default 250 Hz) and turns them into events, so scrolling is even and does not
  depend on the driver's event rate.
- The daemon pauses its output while its own session is not the active one (several users on one
  PC) and has a safety net that releases keys and buttons it still holds when the device is
  quiet, see [docs/troubleshooting.md](docs/troubleshooting.md).
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
