<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Supported systems and requirements

What you need on each operating system before dofscroll can work. Read this before [Installation](Installation).

**Status in short.** Only one setup has been run on real hardware by the maintainer: Linux (RHEL 9, X11, KDE Plasma) with a SpaceMouse Pro and a SpaceMouse Compact. Everything else is built and unit-tested in CI but has not been run with a device. Statements below are marked **verified** (run by the maintainer on hardware), **CI** (checked by a build or a unit test, no device involved) or **unverified** (taken from documentation or from reading the code; nobody has tried it on hardware).

## Overview

| OS | What runs | What you need | Status |
|---|---|---|---|
| Linux, X11 | `dofscroll` daemon (XTest output) and `dofscroll-ui` | `spacenavd` running, a SpaceMouse, systemd user session (for the shipped units) | Verified on RHEL 9, KDE Plasma, SpaceMouse Pro and Compact. Other distributions: packages are built and install-tested in CI only |
| Linux, Wayland | Same programs, uinput output | The above, plus write access to `/dev/uinput` (opt-in udev rule); `kdotool` for program profiles on KDE | Not tested on hardware |
| Windows 10 / 11 | `dofscroll.exe` daemon (SendInput) and `dofscroll-ui.exe` | A SpaceMouse. No SDK and no vendor driver are required by dofscroll | Built by CI only, not run on hardware |
| macOS | `dofscroll.app` with daemon (CGEvent) and interface | A SpaceMouse, Apple silicon, **Input Monitoring** and **Accessibility** permissions | Built by CI only, not run on hardware |

## Hardware: supported devices

dofscroll knows these models (description files in [devices/](https://github.com/HoZe-Lukas/dofScroll/tree/main/devices), factory button counts in `src/device_db.cpp`):

| Model | USB ids (vendor:product) | Configurable buttons |
|---|---|---|
| SpaceMouse Compact | 256f:c635 | 2 |
| SpaceMouse Wireless | 256f:c62e, 256f:c62f, 256f:c63a | 2 |
| SpaceMouse Pro | 046d:c62b | 15 |
| SpaceMouse Pro Wireless | 256f:c631, 256f:c632, 256f:c638 | 15 |
| SpaceMouse Enterprise | 256f:c633 | 31 |
| SpaceNavigator | 046d:c626 | 2 |

- **Linux:** any model that `spacenavd` supports delivers motion; the table above only matters for the button count. Models that are not in the table still work for the axes, with the button count the driver reports.
- **Windows and macOS:** only devices whose vendor and product id appear in a `devices/*.conf` file are opened. A model that is not listed is ignored. You can add one with a description file, see [Devices](Devices).
- Verified on hardware: SpaceMouse Pro and SpaceMouse Compact, on Linux only. All other models are untested. The button counts come from manufacturer pages, not from a device. The raw report layout in the description files (used on Windows and macOS) has **not** been verified on hardware, and axis signs relative to Linux are unverified.
- SpaceExplorer and SpacePilot Pro are deliberately not listed: no button count could be verified.

## Linux

### Input daemon: spacenavd and libspnav (required)

The daemon reads the device through `spacenavd` (the open-source SpaceMouse daemon) and `libspnav`. Without a running `spacenavd` dofscroll sees no device.

- RPM and DEB packages declare the dependency `spacenavd`; `libspnav` comes with it. On RHEL 9 and its clones `spacenavd` is in EPEL (`sudo dnf install -y epel-release` first). Whether `spacenavd` exists in your Debian or Ubuntu release is **unverified**. On Rocky Linux 10 the RPM does not install because `spacenavd` is not in its repositories (found by the install smoke test; this is the only distribution result that is known to fail). With the TGZ you install the dependencies yourself.
- Enable it: `sudo systemctl enable --now spacenavd`.
- Check it: `systemctl status spacenavd` must say active; the dofscroll log (`journalctl --user -u dofscroll.service -f`) shows the device name and button count when the device connects.
- Button mappings that are the same everywhere (for example a button that sends Esc) belong in spacenavd's `spnavrc`, see [Buttons](Buttons).
- `spacenavd` reports only one device when several are attached; dofscroll counts attached devices through `/proc`.

### Session type and output backend

| | X11 | Wayland |
|---|---|---|
| Output | XTest | uinput (virtual input device) |
| Extra requirement | None | Write access to `/dev/uinput`, see below |
| Program profiles (`[app:*]`) | Yes (window class) | Only with `kdotool` on KDE; native Wayland windows may not be found |
| Status | Verified (KDE Plasma) | Not tested |

- `BACKEND = auto` (default) reads `XDG_SESSION_TYPE`: `wayland` selects uinput first, anything else selects X11 first. If the chosen backend fails, the other is tried. You can force `BACKEND = x11` or `uinput` in `[global]` (needs a daemon restart). The log says which backend was chosen and why.
- Under X11 `/dev/uinput` is not needed at all.

### Permissions: /dev/uinput (Wayland, or `BACKEND = uinput`)

- The package ships the udev rule as an example only (`/usr/share/dofscroll/udev/70-dofscroll-uinput.rules`); it is **not** active. Activate it yourself: copy it to `/etc/udev/rules.d/`, run `sudo udevadm control --reload && sudo udevadm trigger`, then log out and in.
- The rule uses `uaccess`: the user of the active local session at the seat gets an ACL. An SSH session gets none.
- If `/dev/uinput` does not exist: `sudo modprobe uinput`.
- Do **not** add yourself to the `input` group: it lets any of your processes read all keyboards. Details: [Security](Security) and `packaging/README-uinput.md` in the repository.
- Status: the rule and the `uaccess` mechanism are documented but have not been tried on a Wayland machine.

### Service manager

The shipped units (`dofscroll.service`, `dofscroll-ui.service`) are systemd user units wanted by `graphical-session.target`. They start with a graphical login, not with a plain SSH login. If you do not use systemd you can start `dofscroll` and `dofscroll-ui` from a terminal or your own autostart; that is **unverified**.

### Tray

`dofscroll-ui` uses the StatusNotifierItem (AppIndicator) protocol.

- KDE Plasma: supported (verified).
- GNOME: install and enable the AppIndicator extension (on RHEL `gnome-shell-extension-appindicator`; the package name elsewhere is unverified). Not tested on GNOME.
- Without a tray the window opens minimized and closing it quits the interface; the status bar says so.
- The daemon works without the tray. The interface is needed for the settings window and for the first-start seeding of the config. Without it, copy `examples/default/dofscroll.conf` to `~/.config/dofscroll/dofscroll.conf` yourself.

### Qt runtime

`dofscroll-ui` is a Qt 6 program, and **the Linux packages (RPM, DEB, TGZ) contain their own Qt 6.9.3** and the libraries it needs, in `/usr/lib/dofscroll` (since 1.0.1; the 1.0.0 RPM required Qt 6.10 from the system). Nothing to install for Qt, no EPEL for Qt, and the Qt of your distribution is not used. It works on RHEL 9 with any Qt 6 or none, and the same on Debian 12 and Ubuntu 24.04. Required from the system: glibc and `libstdc++` (always there), `spacenavd` (the device service, EPEL on the RHEL family) and the daemon's `libspnav`, `libX11`, `libXtst` (the package manager installs them). The TGZ runs the interface from any folder with nothing but the C library. Qt is LGPL-3.0, dynamically linked and replaceable; license texts and the list of bundled libraries are in `/usr/share/doc/dofscroll/third-party/`. Qt 6.4 or newer is needed only to build from source. The package build is not verified on a real desktop (tray, KDE look and Wayland need a real desktop).

### Optional: kdotool

Program profiles need a query of the active window. X11 always has one. On KDE Wayland dofscroll uses `kdotool` if it finds it; it looks only in `/usr/bin`, `/usr/local/bin` and `/bin`, not in `PATH`, so a copy in `~/.local/bin` is ignored. Without a window query only `[global]` and `[device:*]` apply. No other helper tool (such as `xdotool`) is used.

### Before you start (Linux)

- [ ] A SpaceMouse model from the table, plugged in
- [ ] `spacenavd` installed and `systemctl status spacenavd` active
- [ ] A graphical systemd user session
- [ ] X11: nothing more. Wayland: the uinput udev rule activated and `/dev/uinput` writable by you, `kdotool` installed if you want program profiles on KDE
- [ ] Tray: KDE works; on GNOME install the AppIndicator extension

## Windows 10 and 11

### Device access: no SDK, no driver needed by dofscroll

dofscroll reads the SpaceMouse directly as a HID device (SetupAPI and `ReadFile`, opened read-only with sharing allowed). It does **not** use, link or bundle the 3Dconnexion SDK (its license is not compatible with GPL-3.0-or-later). It needs neither `spacenavd` nor an administrator account: the installer is per-user (default folder `%LOCALAPPDATA%\Programs\dofscroll`) and the Run-key autostart needs no elevation. The code is built for Windows 10 or newer (`_WIN32_WINNT=0x0A00`); the CI builds run on `windows-latest`. No hardware run has happened.

### The official 3Dconnexion driver (3DxWare / 3Dconnexion Software)

What is known and what is not:

- **Known (documentation, not hardware):** the official driver has per-application profiles and can translate SpaceMouse motion into scroll, zoom or keys for some programs. For programs without 3D support (browsers, PDF viewers) its generic scrolling is not documented and forum reports are mixed (old, 2007-2016, and partly for other driver versions).
- **Known from the code:** dofscroll never opens the device exclusively. A failed open is logged with the Windows error and retried at each rescan.
- **Community reports (not official, not run by us):** other HID-based tools report that on Windows the device can be read directly while the 3DxWare driver stays installed and running, see [Sources](#sources) 1 to 3. So the expectation is that the driver may stay installed; the part that causes trouble elsewhere is the vendor SDK, which dofscroll does not use.
- **UNRESOLVED for dofscroll (nobody has tested it):** whether, with a current driver version, the driver may stay installed while dofscroll runs, and whether both programs receive every report (or the driver opens the device exclusively and dofscroll logs a sharing violation). The Windows checklist tests both cases (driver running, driver stopped).
- **Not a requirement:** dofscroll does not need the driver. Installing it is optional. Whether installing it is a good idea is exactly the unresolved question above.
- **Double input:** a community note warns that with the driver installed it may also emit its own keyboard and mouse emulation, so check for double input (source 2).
- **Axes:** raw HID axes can differ from what the driver delivers (one report: tilt and twist swapped without a driver profile, source 7). If an axis moves the wrong way, adjust it with `<CH>_SIGN` or the mix settings, see [Troubleshooting](Troubleshooting).
- **Do I need the official driver? Does it work together?** No for dofscroll; yes for your CAD programs. Keep it installed and running: dofscroll is designed to work next to it. Whether both really run at the same time on Windows is exactly what the coexistence test finds out (about 5 minutes, it changes nothing and never stops the driver): `powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1` from the install folder. It writes `dofscroll-driver-test-report.txt`; attach it to issue #36. Verification on real Windows machines is still pending and is the top priority.
- **If scrolling happens twice in a program:** switch dofscroll off for that program (`ENABLED = 0` in its `[app:...]` profile). Please also report it with the test file.
- **If the device cannot be opened:** the daemon logs the Windows error (32 sharing violation or 5 access denied) in `%LOCALAPPDATA%\dofscroll\dofscroll.log` and the interface shows "SpaceMouse found but cannot be read". If both programs cannot read the device at the same time, that is a bug of dofscroll to be fixed: please report it with the test file. See [docs/troubleshooting.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/troubleshooting.md#the-official-3dconnexion-driver-and-dofscroll).

### Permissions and Windows security

- The installer and the ZIP are **not code-signed**. SmartScreen shows "Windows protected your PC": click **More info**, then **Run anyway**. Antivirus tools may flag unsigned programs more often. No administrator rights are needed.
- Input sent to an elevated (administrator) window is blocked by Windows (UIPI) unless dofscroll runs elevated too; the lock screen and UAC prompts cannot be injected into. This is expected behavior, unverified in practice.
- Program profiles use the foreground executable name (for example `firefox.exe`). Packaged (UWP) apps may be reported as `ApplicationFrameHost.exe`. Unverified.

### Tray and Qt

- The tray icon is a standard Windows notification-area icon; Windows may hide it in the overflow area (the `^` arrow) until you drag it out. Unverified on hardware.
- Without a tray the window opens minimized and closing it quits `dofscroll-ui.exe`.
- The installer includes the Qt libraries; nothing else to install. If Qt was missing when the package was built, the file name contains `-daemon-only` and there is no settings window.
- The daemon `dofscroll.exe` has no window. Autostart is the optional installer component "Start the daemon at logon" (off by default) or the "Start at login" switch in the interface.

### Before you start (Windows)

- [ ] A SpaceMouse model from the table, plugged in
- [ ] Windows 10 or 11
- [ ] You accept the SmartScreen warning for the unsigned installer
- [ ] The 3Dconnexion driver is optional for dofscroll. If you have it (you need it for CAD programs): keep it running and run the coexistence test (section above)
- [ ] You know the result is untested on hardware: please report what you see

## macOS

### Device access: no SDK, no driver needed by dofscroll

dofscroll reads the SpaceMouse through `IOHIDManager`, matched on the vendor and product ids of the `devices/*.conf` files. It does **not** use the proprietary `3DconnexionClient` framework. The release image is built for Apple silicon (`arm64`) on the `macos-latest` runner; an Intel build is not provided and the minimum macOS version is not stated anywhere in the repository. The device is opened without seizing it.

### The official 3Dconnexion driver (3Dconnexion Software)

- **Known (forum, old):** with the driver installed, SpaceMouse behavior in some applications differs; a moderator said scrolling in Safari and Acrobat Reader was unsupported with 3DxWare 10.8.4. Outdated, current behavior unknown.
- **Community reports (not official, not run by us):** on macOS the picture is worse than on Windows. Programs that open the device through hidapi report that the 3Dconnexion helper holds the device exclusively and they get no reports until the helper is quit (CloudCompare, source 5). hidapi seizes the device by default; one project switched to shared mode (hidapi 0.12 or newer) to open it while the driver runs, but its author had not tested that on a Mac (source 4). One tool for macOS stops the driver's helper while it runs (source 1).
- **Our code:** dofscroll opens the device without seizing it (`kIOHIDOptionsTypeNone`, never the seize option). Whether that mode delivers reports while the driver is running is **UNRESOLVED**: the code comment says so, and nobody has tried it. The macOS checklist tests both cases.
- **Not a requirement:** dofscroll does not need the driver.
- **Do I need the official driver? Does it work together?** No for dofscroll; yes for your CAD programs. Keep it installed and running: dofscroll is designed to work next to it. Whether both really run at the same time on a Mac is exactly what the coexistence test finds out (about 5 minutes, it changes nothing and never stops the driver): `sh /Applications/dofscroll.app/Contents/Resources/diagnostics/test-driver-coexistence.sh`. It writes `dofscroll-driver-test-report.txt`; attach it to issue #37. Verification on real Macs is still pending and is the top priority.
- **If scrolling happens twice in a program:** switch dofscroll off for that program (`ENABLED = 0` in its `[app:...]` profile). Please also report it with the test file.
- **If the device cannot be read** (no input although the permissions are granted): the daemon logs `kIOReturnExclusiveAccess` and the interface shows "SpaceMouse found but cannot be read". If the driver's helper holds the device, that is a bug of dofscroll to be fixed: please report it with the test file. The options if the test confirms it are in [docs/port-architecture.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-architecture.md#if-the-driver-holds-the-device-exclusively).

### Permissions

macOS asks for two permissions, per binary:

1. **Input Monitoring** to read the device (the code uses `IOHIDCheckAccess` and `IOHIDRequestAccess`).
2. **Accessibility** to post scroll, key and mouse events (`AXIsProcessTrusted`).

Grant both in System Settings > Privacy & Security to the daemon inside the app: `/Applications/dofscroll.app/Contents/Resources/daemon/dofscroll`, then restart the daemon. The grants are tied to the binary's path and code signature; moving it, replacing it or rebuilding an unsigned binary can invalidate them (symptom: a running daemon that produces no input or no output). The daemon re-checks the permissions every 5 seconds and writes a missing one to the log and status file instead of failing silently. The exact prompts and pane names per macOS version are **unverified**.

The app is only ad-hoc signed and **not notarized**: Gatekeeper blocks the first start. Right-click the app > **Open** > **Open**; on macOS 15 and later use System Settings > Privacy & Security > **Open Anyway**; or run `xattr -dr com.apple.quarantine /Applications/dofscroll.app`.

### Tray and Qt

- The tray is a menu bar icon (template icon). macOS does not deliver a click on an icon that has a menu, so open the window with **Settings...** in the menu. There is no Dock icon.
- The app bundle includes the Qt libraries; nothing else to install.
- Unverified on hardware.

### Before you start (macOS)

- [ ] A SpaceMouse model from the table, plugged in
- [ ] An Apple silicon Mac
- [ ] You can approve Gatekeeper's "Open Anyway" for the unsigned app
- [ ] You will grant Input Monitoring and Accessibility to the daemon binary inside the app
- [ ] The 3Dconnexion driver is optional for dofscroll; if installed (CAD programs need it), keep it running and run the coexistence test (section above)
- [ ] You know the result is untested on hardware: please report what you see

## Driver versus SDK, and what the research found

- The **SDK** (the vendor API that applications link against) is not used and not bundled by dofscroll. This is a license decision, see `docs/windows-mac.md`.
- The **driver** (3DxWare / 3Dconnexion Software) is an installed program. dofscroll does not need it. Whether it can stay installed is the open question, and the answer differs by OS in community reports: Windows, probably yes; macOS, possibly not (the helper may hold the device exclusively).
- On Linux there is no vendor driver. `spacenavd` is the open-source replacement for the 3Dconnexion driver and SDK and supports USB 3Dconnexion devices on GNU/Linux (source 8). dofscroll uses it instead of the vendor driver.
- Nothing here replaces a hardware test. dofscroll's own checklists are `docs/port-windows-checklist.md` and `docs/port-macos-checklist.md`.

## Sources

Community sources only; the official 3Dconnexion pages could not be fetched, and no official statement on coexistence with other HID readers was found. Pages 1, 4, 5 and 8 were opened while writing; 2, 3 and 7 are second-hand and could not be re-opened.

1. bogumat/3dmouse (Python library; says it reads the HID device alongside 3DxWare on Windows without taking over, and stops the driver helper on macOS): https://github.com/bogumat/3dmouse
2. RawMouse plugin for Cura (README notes on stopping 3DxWare, reports that Windows does not need it, and driver emulation causing double input); not re-opened, link not verified.
3. Godot SpaceMouse plugin (says the driver is not needed on any OS); not re-opened, link not verified.
4. QElectroTech pull request 1028, 3D mouse not detected on macOS while 3DxWare is running: https://github.com/qelectrotech/qelectrotech-source-mirror/pull/1028
5. CloudCompare pull request 2339 (hidapi path; helper holds the device exclusively on macOS): https://github.com/CloudCompare/CloudCompare/pull/2339
6. OpenFollow issue 175 (hidapi readers see reports only after quitting the helper): the page could not be opened (HTTP 404), so it is not relied on.
7. Axis difference without a driver profile (solidcraft issue 1172): the page could not be opened (HTTP 404), so treat as an unconfirmed report.
8. spacenav project FAQ: https://spacenav.sourceforge.net/faq.html
9. Repository notes: `docs/port-windows.md`, `docs/port-macos.md`, `docs/windows-mac.md`.

## Building from source

The build needs a C++17 compiler, CMake 3.16 or newer, `libspnav` and the X11 and XTest development headers on Linux, `gettext` for the German translation, and Qt 6.4 or newer (Widgets, with SVG) for the interface. Package names per distribution and the `check-deps` target are in [Installation](Installation).

## What only a human can verify

Everything marked unverified on this page, in particular: scrolling, zoom and buttons on any real device on Windows and macOS; the interaction with the official 3Dconnexion driver on both; the macOS permission prompts; the Wayland (uinput) path and GNOME tray on Linux; and every distribution other than RHEL 9. Test lists: `docs/port-windows-checklist.md` and `docs/port-macos-checklist.md` in the repository. See also [Troubleshooting](Troubleshooting) and [Devices](Devices).
