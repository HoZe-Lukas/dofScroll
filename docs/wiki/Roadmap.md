<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Roadmap and status

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Source of truth: [docs/work-plan.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/work-plan.md) (state as of 2026-10-19). If this page and the work plan differ, the work plan wins.

## Parity rule

Every user-visible feature must work on Linux, Windows and macOS. A feature is "done" only when all three have an implementation behind the same interface and the same tests (platform parts compile on their runner). Toolkit-free logic is shared; platform parts stay small. "Done" means built and tested in CI; behavior on real hardware is confirmed separately by a human.

## Done

- Modes plugin system, `ModeRunner`, all keys and mouse buttons on X11 and uinput
- First-start seeding of the config and example modes
- Daemon split into a platform-independent core and a thin Linux main; platform interfaces with a fake for tests
- GTK tray with application modes, gettext (English source, German catalog), start-at-login checkbox
- Device database, HID descriptor parser, `devices/*.conf`, `dofscroll-hidprobe`
- 1-based `BUTTON_<n>` numbering everywhere
- CMake build, CPack packages (RPM, DEB, TGZ), CI with sanitizers, cppcheck, secret scan, release workflow
- Security review report and first fixes

## Implemented, CI-verified, hardware-unverified

- **Windows port (W1):** raw HID input, `SendInput` output, foreground-window provider, named-event reload, no-console daemon, per-user autostart script. Manual tests: [port-windows-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows-checklist.md).
- **macOS port (W2):** IOHID input, CGEvent output, NSWorkspace provider, LaunchAgent, permission handling. Manual tests: [port-macos-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos-checklist.md).

## In progress

- **Qt6 interface (W3):** one tray and settings window for all three systems (skeleton `dofscroll-ui`, navigation, running-program picker model). The GTK tray stays on Linux until the Qt interface has parity.

## Planned

| Package | Content |
|---|---|
| W4 | Unsigned installers including Qt: Windows per-user installer and ZIP, macOS app/dmg, Linux UI subpackage |
| W9 | Verify device descriptors and factory button counts on real hardware |
| W17 | Running-program picker for Windows and macOS |
| W18 | Start at login on Windows and macOS (and the Qt UI on Linux) |
| W7 / W8 | Follow-up security fixes; keep documentation in sync |

Decisions: version 0.1 is released without waiting for the Qt interface (Windows and macOS daemons configured by file); the shared interface follows in 0.2. 0.1 installers are unsigned and not notarized. Known limitation: on Linux `spacenavd` reports one merged device, so two mice with different modes at once need direct HID access.

Pick up work through the claiming rules in the work plan; see [Development](Development).
