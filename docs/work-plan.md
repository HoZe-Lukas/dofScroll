<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Work plan and coordination

Single source of truth for who works on what. Read `AGENTS.md` (rules) and `readme.md` (behavior)
first. Several people or AI agents may work in parallel; this file keeps them from colliding.

## Rules in short
- Everything in the repository is **English**; the only exception is the German catalog `po/de.po`.
- License: GPL-3.0-or-later; new files carry an SPDX header (see any file in `src/`).
- Do **not** bundle the proprietary 3Dconnexion SDK. Input on Windows/macOS is raw HID / IOHID.
- The number of configurable buttons is the **factory** count of the device model
  (`factoryButtonCount()` in `src/device_db.hpp`), not the count the hardware or spacenavd reports.
- No personal data, hostnames or e-mail addresses in files; names only in SPDX lines.
- Real-device behavior (scrolling, buttons, tray) can only be verified by a human on hardware.
  Never claim it was tested if it was not.

## Claiming work
1. Pick a package below whose status is `open`.
2. Create a branch `feat/<package-id>` and change the status line to `in progress (<who>, <date>)`
   in your first commit; push it.
3. Touch only the files listed for the package. Need a change elsewhere? Open an issue or note it
   in the package line instead of editing another package's files.
4. `make check` (or `cmake --build ... && ctest`) must pass before you ask for a merge.
5. One integrator merges into `main`; rebase on `main` before asking.

## State as of 2026-10-19
Done and on `main`: modes plugin system (`src/modes.*`, `examples/modes/`), `ModeRunner` in the
daemon, all keys/mouse buttons on X11 and uinput (`src/key_tables.hpp`), first-start seeding
(`src/seed.*`), tray with "Application modes" list and gettext (English source, German catalog in
`po/`), device database and HID descriptor parser (`src/device_db.*`, `src/hid_descriptor.*`,
`devices/*.conf`), CMake + CPack + GitHub Actions workflow, English documentation, port documents in
`docs/` (`port-architecture.md`, `port-windows.md`, `port-macos.md`).

Known limitation: spacenavd reports only one device, so two mice with different modes at the same
time are not possible on Linux until direct HID access exists (package W1/W2 design).

## Open packages

| Id | Package | Files (owner of) | Depends on | Status |
|---|---|---|---|---|
| W0a | Platform interfaces: `src/platform/platform.hpp` (`InputSource`, `ActiveWindowProvider`, paths, `ReloadListener`) and `src/platform/fake_input.hpp` + `tests/test_platform.cpp` | done 2026-10-19 | - | **done** |
| W0b | Linux behind the interfaces: spacenavd `InputSource`, X11 `ActiveWindowProvider`, XDG paths, SIGHUP-based reload; split `src/dofscroll.cpp` into a platform-independent daemon core plus a thin `main` per OS | `src/dofscroll.cpp`, new `src/daemon_core.*`, new `src/platform/linux/`, `src/config.cpp` (paths only) | W0a | in progress (session 1, 2026-10-19) |
| W1 | Windows port: raw HID `InputSource` (use `src/hid_descriptor.*` and `devices/*.conf`), `SendInput` `Output`, foreground-window `ActiveWindowProvider`, paths, named-event `ReloadListener`, no-console daemon `main`, autostart | new `src/platform/win/`, `cmake/windows.cmake`, `packaging/windows/` | W0a done; the final daemon `main` waits for W0b | open |
| W2 | macOS port: IOHID `InputSource`, CGEvent `Output`, `NSWorkspace` `ActiveWindowProvider`, paths, `ReloadListener`, LaunchAgent, permissions | new `src/platform/mac/`, `cmake/macos.cmake`, `packaging/macos/` | W0a done; the final daemon `main` waits for W0b | open |
| W3 | Per-OS tray / settings UI (native minimal tray first) | new `src/ui/` | W0 | open |
| W4 | Installers and signing: NSIS, macOS pkg/dmg, notarization, release workflow | `packaging/`, `.github/workflows/release.yml` | W1, W2 | open |
| W5 | Translate the axis wizard's dynamic result messages (numbers inside sentences) | `src/axis_wizard.hpp`, `src/dofscroll_tray.cpp`, `po/` | - | open |
| W6 | Use one numbering for `BUTTON_<n>`: config files count from 1, mode files from 0 today; unify to 1-based | `src/modes.*`, `src/config.*`, `examples/modes/`, tests, docs | - | open |
| W7 | Code review and security audit: modes directory loading, seeding, key output (`key:` actions), zoom Ctrl handling across channels, systemd sandbox | `docs/security.md`, fixes in affected files | - | open |
| W8 | Keep documentation in sync with final behavior (`readme.md`, `docs/`) | `readme.md`, `docs/` | after each feature | open |
| W9 | Verify device descriptors and factory button counts on real hardware; add SpaceExplorer / SpacePilot Pro counts once verified | `devices/*.conf`, `src/device_db.cpp` | hardware | open |
| W10 | CI hardening: ASAN job, secret scan, cppcheck, package builds on tags | `.github/workflows/`, `CMakeLists.txt` | - | open |
| W11 | Avoid the Xlib `None` macro clash in `src/modes.hpp` (daemon currently uses `#undef None`) | `src/modes.hpp`, `src/dofscroll.cpp` | - | open |

Order that avoids conflicts: W0b touches core files and is merged first; W1/W2 start now with
backends in new files (they only depend on the interfaces from W0a) and wire the daemon `main` once
W0b is on `main`. Backends must be unit-testable without hardware (see `FakeInputSource`). W5-W11 are independent of each other.
