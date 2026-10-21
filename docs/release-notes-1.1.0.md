<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.1.0

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers (Rocky Linux 9, Debian 12 and 13, Ubuntu 24.04,
> Fedora 42). The **Windows and macOS builds are built and unit-tested by CI but have never been run with a
> real device.**

Feature release after [1.0.0](release-notes-1.0.0.md). A version 1.0.1 was prepared but never published; its
changes are part of this release. Highlights: the Linux packages bring their own Qt, Windows and macOS can read the
device through the official 3Dconnexion driver, several SpaceMice can run side by side with their own profile and
joystick mode (Windows and macOS, direct HID), five new joystick modes, and the interface now stays in the tray
after login. The Linux daemon, the command line and all existing configuration keys behave as before; one new key,
`INPUT`, is read on Windows and macOS only.

## What changed

### Several SpaceMice (#93)

- **One lane per device.** Where the devices arrive separately (Windows and macOS, direct HID) the daemon keeps a
  lane for each attached SpaceMouse: its own profile (`[device:*]` applies to the right device), joystick mode,
  hold, toggle and navigation latch, DSP state and safety net. A button on one device switches the joystick mode
  of that device only. Details: [multi-device.md](multi-device.md).
- **Shared keys are counted.** Ctrl for zoom stays pressed until the last device stops zooming; unplugging a
  device, a joystick mode change or `ENABLED = 0` for one device never releases a key another device still holds.
- **One device per model:** a Pro next to a Compact works; a second device of the same model is ignored with a log
  line. Hotplug keeps the state of the other devices.
- The status line shows all devices with their joystick mode ("Devices: A (Joystick mode: normal), B (...)");
  the status file gets `lanes=`, `active_device=` and `device<k>=`, `mode<k>=` and more for k = 2..n.
- **Not done:** on Linux `spacenavd` still merges all devices into one stream, so Linux has one lane and
  `[device:*]` applies to all devices; a per-device path next to `spacenavd` was not built (see
  [multi-device.md](multi-device.md)). The Axes page lists all device names and shows the device that moved last;
  it has no device selector. Everything is covered by tests with a fake two-device source; it has **not**
  been run with two real devices (issue #93 stays open for that).

### New joystick modes (#86 to #90)

`sheet` (arrow keys for spreadsheet cells, twist pages up and down), `photo` (sideways = previous/next image, up/down
zooms), `zoom-pan` (up/down zooms, twist scrolls sideways), `vertical-only` (vertical scroll only) and `fast` (the
counterpart of `precision`: high sensitivities). They are built in on every system like the others
([modes.md](modes.md)); none was tried in the programs they are meant for.

### Interface stays in the tray after login (#135)

On KDE the interface could open with a large window after every login: the desktop's session restore started a
second `dofscroll-ui` next to the systemd unit, and the second start asked the first to show its window. The
interface now opts out of session restore and a start with `-session` never raises the window
([troubleshooting.md](troubleshooting.md)). Only a real login shows whether this fixes it on your desktop.

### Extensions page (#129)

The interface has one page for extensions: joystick mode files, device descriptions and examples, each with a switch
([ui.md](ui.md)).

### Linux packages

- **The Linux packages bring their own Qt (#116).** The 1.0.0 RPM was built against Qt 6.10.3 from EPEL, so it
  required `libQt6Core.so.6(Qt_6.10)`: a RHEL 9 system with an older Qt 6 (for example Qt 6.6.2 from an EPEL
  mirror) could only install it by upgrading the whole Qt 6 stack, or not at all. Now the RPM, the DEB and the
  TGZ contain **Qt 6.9.3** and every library it needs in `/usr/lib/dofscroll` (Qt, ICU, the X11 and Wayland
  platform plugins, libX11 and the xcb libraries, xkbcommon, fontconfig, freetype, harfbuzz, glib, dbus, ...). They
  need no Qt from the system, none from EPEL, and do not use one that is installed: they install on RHEL 9 with
  any Qt 6 or none, and the same on Debian 12 and Ubuntu 24.04. The package dependencies are `spacenavd` (the
  device service, from EPEL on the RHEL family) and the daemon's libraries; glibc and `libstdc++` stay on the system.
  Packages are about 27 MB (RPM), 34 MB (DEB) and 35 MB (TGZ).
- **The TGZ is self-contained**: unpack it anywhere and run `usr/bin/dofscroll-ui`; it needs only the C library
  (checked in bare Rocky 9, Debian 12, Ubuntu 24.04 and Fedora 42 containers).
- **One Qt version everywhere.** Linux, Windows and macOS use Qt 6.9.3 (Windows moved from 6.8.3), set in one
  file (`packaging/qt.env`) that all CI jobs read; a weekly check warns about new Qt patch releases, because a
  Qt security update is now delivered by a dofscroll release ([release-process.md](release-process.md)).
- `dofscroll-ui --version` prints the Qt it runs on on a second line (the first line is unchanged).
- Licenses: the packages ship the license texts of Qt (LGPL-3.0, dynamically linked and replaceable) and of every
  bundled library, an inventory `bundled-libraries.txt` and `THIRD-PARTY-NOTICES.md` in
  `/usr/share/doc/dofscroll/` (and next to the program on Windows and macOS).
- CI: the package job fails if the RPM requires Qt or a bundled library or provides one; the install smoke test
  runs the interface self-test on five distributions and the TGZ on four, and checks that Qt is loaded from the
  package.
- The DEB depends no longer on `libqt6*` packages of the distribution.

### Windows and macOS: working next to the official driver (#122)

- **New key `INPUT = auto | driver | hid`** (`[global]`, default `auto`, ignored on Linux). `driver` reads the
  device through the interface of the installed official 3Dconnexion driver, `hid` reads it directly as raw HID,
  `auto` picks by a built-in table (macOS: driver first when installed; Windows: HID first, the driver as the
  fallback) and falls back without flapping. The official driver stays installed and running; dofscroll never asks
  you to stop it. Nothing of the vendor is shipped: the driver library is loaded at run time from where the
  driver is installed ([legal-notes.md](legal-notes.md)).
- The active path is shown as `Input: 3Dconnexion driver` or `Input: direct (HID)` on the Overview, in the new
  **Input path** setting (Windows and macOS only), in the tray tooltip, in the log and as `input=` in the status
  file. The Settings page also has an About box (version, licenses, Qt under the LGPL, 3Dconnexion note).
- **`dofscroll-hidprobe` now exists on Windows and macOS** and ships in the installer, the ZIP and the DMG. A
  guided five-minute test (`test-driver-coexistence.ps1` / `.sh`, shipped as well) checks whether dofscroll and the
  driver work side by side and writes a report you can attach to an issue.
- When the device cannot be read (held by another program) or is open but silent, the daemon logs it, sets
  `input_issue=blocked|silent` in the status file, and the Overview shows a warning. The remedy is `ENABLED = 0`
  for the program in question, not quitting the driver ([troubleshooting.md](troubleshooting.md)).
- Windows moved from Qt 6.8.3 to 6.9.3 (see above).

### Qt interface

- **Split push and tilt per channel (#100, #115, #117):** the joystick mode editor has a "Split push and tilt"
  switch for the vertical and the horizontal scroll channel, with an action and a tuning box for each part. The
  Axes page shows live bars for the push and the tilt part of a split channel.
- A warning next to an axis mix that uses lift/press (`y`), which is prone to false triggers (#94, #113).
- **Accessibility (#104):** status messages carry a symbol as well as colour and words (#119); the sidebar has a
  focus ring and moves with Up, Down, Home and End (#121); self-tests check target sizes of at least 24 x 24 px,
  tab order and the information popovers (#121); lengths in the style sheet are relative (em), so the layout
  follows the system font size, and a 200 % font self-test found and fixed a too wide joystick mode drop-down on
  the Profiles page (#126). Not yet covered: high-contrast screenshots and a review with screen readers,
  magnifiers and a colour-blind reviewer (they need people and real devices; #104 stays open).
- `dofscroll-ui --version` prints the Qt it runs on a second line (the first line is unchanged).
- The docs and the wiki say "joystick mode" and "button mode" everywhere, checked in CI (#125).

### Website and CI

- New, lighter website design, English and German (#111, #112, #114, #118).
- CI: the package job fails if the RPM requires Qt or a bundled library or provides one; the install smoke test
  runs the interface self-test on five distributions and the TGZ on four and checks that Qt is loaded from the
  package; a MinGW test warning (GCC dangling pointer, test code only) was fixed (#131).

## Upgrading

`sudo dnf install ./dofscroll-1.1.0-1.x86_64.rpm` (EPEL enabled for `spacenavd`, see
[installation.md](installation.md)); `sudo apt install ./dofscroll_1.1.0_amd64.deb`. An older Qt that the
1.0.0 RPM pulled in stays installed and is simply no longer used by dofscroll.

## Tested on and known limitations

As for 1.0.0. **Several SpaceMice have never been run with two real devices**, and the autostart fix and the new
joystick modes have not been checked on a real desktop or in the target programs. **The driver path (`INPUT`) has never run against the real 3Dconnexion driver on Windows or
macOS; the defaults of `auto` are unverified guesses**, and so is everything about the new hidprobe backends and
the coexistence test (checklists in [port-windows-checklist.md](port-windows-checklist.md) and
[port-macos-checklist.md](port-macos-checklist.md)). The new Qt interface features were tested offscreen and in
CI only, not on a real desktop. The new packages were built and install-tested in CI containers (build, install, `dofscroll-ui
--self-test` offscreen, X11 and Wayland plugins load, TGZ in bare images) and the interface was run against a
virtual X server. Not verified: the tray icon, the look under KDE Plasma and GNOME with the bundled Qt (Qt's own
Fusion style with the desktop's light or dark scheme, no KDE or GNOME style plugin), native Wayland sessions, and
installation on a real RHEL 9 machine; only the maintainer can do that.
