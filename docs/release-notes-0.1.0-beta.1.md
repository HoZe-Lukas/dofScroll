<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 0.1.0-beta.1 (draft release notes)

Pre-release. Not a stable release. Summary of what is on `main` since `v0.1.0-rc2`.

dofscroll turns a 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys and mouse
buttons for ordinary desktop programs. It complements the vendor driver instead of duplicating
it: a button mapping that is the same everywhere and always belongs in the driver; dofscroll
handles axis shaping, keys derived from axes, mode-dependent bindings and per-program and
per-device profiles.

## Tested on

| Platform | Status |
|---|---|
| RHEL 9, X11, KDE Plasma, SpaceMouse Pro and SpaceMouse Compact | Tested by the maintainer on real hardware |
| Other Linux distributions, Wayland | Not tested (packages are built by CI only; Wayland needs the opt-in uinput udev rule) |
| Windows | Built by CI (daemon, Qt interface, installer, ZIP). **Not run on hardware** |
| macOS | Built by CI (daemon, Qt interface, DMG for Apple silicon). **Not run on hardware** |
| Other SpaceMouse models | Not tested |

## What is new

- **Qt6 interface `dofscroll-ui`** for Linux, Windows and macOS: tray icon and settings window
  with the pages Channels, General, Axes (live bars, axis assistant), Modes (new, duplicate,
  import, edit, delete) and Profiles (device and application profiles, per-profile mode
  dropdown, overrides, button bindings), plus the running-program picker with "Capture next
  focused program". See [ui.md](ui.md).
- **Start at login** on Linux (systemd user units), Windows (per-user Run key) and macOS
  (LaunchAgent), switchable from the interface.
- **Linux packages** (RPM, DEB, TGZ) include `dofscroll-ui`, a menu entry, icon, user unit and
  the German catalog when Qt6 was available at build time; the GTK tray `dofscroll-tray` stays
  as fallback. `dofscroll-ui.service` conflicts with `dofscroll-tray.service`, so run one of them.
- **Unsigned installers:** Windows per-user installer and portable ZIP, macOS DMG (Apple
  silicon). Windows SmartScreen shows "Windows protected your PC": More info, Run anyway.
  macOS Gatekeeper blocks the first start: right-click > Open, or Privacy & Security > Open
  Anyway, or `xattr -dr com.apple.quarantine`. See [installation.md](installation.md).
- **Stuck-key safety net:** the daemon releases every key and button it holds when the device
  has been quiet for 250 ms, has a stale-input watchdog, and both trays have "Release all
  keys" (`dofscroll --release-keys`). See [troubleshooting.md](troubleshooting.md).
- **Output pauses while the own session is not the active one** (several users on one PC),
  with `session=inactive|active` in the status file.
- Qt interface: settings saves notify the daemon on Linux, Windows and macOS; device list and
  factory button count from the daemon's live feed (Profiles and Axes pages); hardened runtime
  directory and mode request file handling.
- Windows and macOS daemons (raw HID / IOHID), device database, `dofscroll-hidprobe`.
- Security review and fixes, UTF-8 policy for all text and paths, English-only check in CI,
  documentation of the vendor-driver principle, wiki sources in `docs/wiki`.

## Known limitations

- Windows and macOS were never run with a real device. The Windows installer is unsigned; the
  macOS DMG is ad-hoc signed and not notarized.
- The Qt interface was not checked on Windows or macOS desktops. On Linux the GTK tray is still
  the documented default.
- The session pause was tested with fake probes only; no second user session was available. The
  Windows and macOS probes are compile-checked only.
- On Linux `spacenavd` reports one merged device, so two mice with different modes at once are
  not possible.
- Wayland is untested; active-window profiles there need `kdotool` (KDE).
- Packages were not installed on Debian, Ubuntu or Fedora.
- Mode files can press any key; use only files from sources you trust.

## Upgrading from 0.1.0-rc2

Install the new package over the old one, run `systemctl --user daemon-reload` and restart the
units. Packages never touch your config. Old `spnav-scroll` paths are still read.

## Checksums

Verify downloads with `sha256sum -c --ignore-missing SHA256SUMS.txt` (protects against damaged
downloads, not against a compromised release).
