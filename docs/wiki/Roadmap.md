<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Roadmap and status

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Source of truth: [docs/work-plan.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/work-plan.md) (state as of the 0.1.0-beta.1 documentation pass). If this page and the work plan differ, the work plan wins.

## Parity rule

Every user-visible feature must work on Linux, Windows and macOS. A feature is "done" only when all three have an implementation behind the same interface and the same tests (platform parts compile on their runner). Toolkit-free logic is shared; platform parts stay small. "Done" means built and tested in CI; behavior on real hardware is confirmed separately by a human.

## Done

- Modes plugin system, `ModeRunner`, all keys and mouse buttons on X11 and uinput
- First-start seeding of the config and example modes
- Daemon split into a platform-independent core and a thin Linux main; platform interfaces with a fake for tests
- Tray with gettext (English source, German catalog) and a start-at-login switch (since 0.1.1 part of the Qt interface; the GTK tray was removed)
- Device database, HID descriptor parser, `devices/*.conf`, `dofscroll-hidprobe`
- 1-based `BUTTON_<n>` numbering everywhere
- CMake build, CPack packages (RPM, DEB, TGZ), CI with sanitizers, cppcheck, secret scan, release workflow
- Security review report and first fixes
- Qt6 interface `dofscroll-ui`: Overview, Profiles, Editor, Joystick modes, Button modes, User modes, Device, Axes and Settings pages, running-program picker, tray, start at login
- Linux packages include the Qt interface when Qt6 is available at build time (the GTK tray was removed in 0.1.1)
- Unsigned Windows installer and ZIP and macOS DMG built by CI
- Stuck-key safety net, `--release-keys`, and pausing output while the own session is not the active one

## Implemented, CI-verified, hardware-unverified

- **Windows port (W1):** raw HID input, `SendInput` output, foreground-window provider, named-event reload, no-console daemon, per-user autostart script. Manual tests: [port-windows-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows-checklist.md).
- **macOS port (W2):** IOHID input, CGEvent output, NSWorkspace provider, LaunchAgent, permission handling. Manual tests: [port-macos-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos-checklist.md).

## Not verified yet

- Everything on Windows and macOS (daemon, interface, installers) on real hardware and desktops
- The Qt interface on Windows and macOS desktops
- Packages on Debian, Ubuntu and Fedora on a real desktop (they are install-tested in CI containers); Wayland

## Planned

| Package | Content |
|---|---|
| W9 | Verify device descriptors and factory button counts on real hardware |
| W7 / W8 | Follow-up security fixes; keep documentation in sync |
| Later | Signed installers |

Decisions: version 0.1 is released without waiting for the Qt interface (Windows and macOS daemons configured by file at first); the Qt interface and unsigned installers shipped with 0.1.0-beta.1 after all. Installers are unsigned and not notarized. Known limitation: on Linux `spacenavd` reports one merged device, so two mice with different modes at once need direct HID access.

Pick up work through the claiming rules in the work plan; see [Development](Development).
