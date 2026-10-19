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
4. `cmake --build build --target check` (or at least `check-fast`) must pass before you ask for a merge.
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
| W0b | Linux behind the interfaces: spacenavd `InputSource`, X11 `ActiveWindowProvider`, XDG paths, SIGHUP-based reload; split `src/dofscroll.cpp` into a platform-independent daemon core plus a thin `main` per OS | `src/dofscroll.cpp`, new `src/daemon_core.*`, new `src/platform/linux/`, `src/config.cpp` (paths only) | W0a | **done** |
| W1 | Windows port: raw HID `InputSource` (use `src/hid_descriptor.*` and `devices/*.conf`), `SendInput` `Output`, foreground-window `ActiveWindowProvider`, paths, named-event `ReloadListener`, no-console daemon `main`, autostart | new `src/platform/win/`, `cmake/windows.cmake`, `packaging/windows/` | W0a done; the final daemon `main` waits for W0b | open |
<<<<<<< HEAD
| W2 | macOS port: IOHID `InputSource`, CGEvent `Output`, `NSWorkspace` `ActiveWindowProvider`, paths, `ReloadListener`, LaunchAgent, permissions | new `src/platform/mac/`, `cmake/macos.cmake`, `packaging/macos/` | W0a done; the final daemon `main` waits for W0b | **implemented, CI-verified; hardware-unverified** (build incl. Objective-C++, unit tests, LaunchAgent plist lint/round trip on `macos-latest`; manual tests: `docs/port-macos-checklist.md`) |
| W3 | Per-OS tray / settings UI (native minimal tray first) | new `src/ui/` | W0 | open |
| W4 | Installers and signing: NSIS, macOS pkg/dmg, notarization, release workflow | `packaging/`, `.github/workflows/release.yml` | W1, W2 | open |
=======
| W2 | macOS port: IOHID `InputSource`, CGEvent `Output`, `NSWorkspace` `ActiveWindowProvider`, paths, `ReloadListener`, LaunchAgent, permissions | new `src/platform/mac/`, `cmake/macos.cmake`, `packaging/macos/` | W0a done; the final daemon `main` waits for W0b | open |
| W3 | Native minimal tray for Windows and macOS (status, mode submenu, enable toggle, open config/log, quit); no settings window in 0.1; prompt in `docs/tray-installer-session-prompt.md` | new `src/ui/`, `cmake/windows.cmake`, `cmake/macos.cmake` (UI parts) | W1, W2 merged | open (starts after the port PRs are merged) |
| W4 | Unsigned installers for 0.1: Windows per-user installer + ZIP, macOS .app/.dmg(.pkg); attach to the GitHub Release; Gatekeeper/SmartScreen steps in `docs/installation.md` | `packaging/windows/`, `packaging/macos/`, `.github/workflows/`, `docs/installation.md`, `docs/release-process.md` | W3 | open (same session as W3) |
>>>>>>> origin/main
| W5 | Translate the axis wizard's dynamic result messages (numbers inside sentences) | `src/axis_wizard.hpp`, `src/dofscroll_tray.cpp`, `po/` | - | **done** |
| W6 | Use one numbering for `BUTTON_<n>`: config files count from 1, mode files from 0 today; unify to 1-based | `src/modes.*`, `src/config.*`, `examples/modes/`, tests, docs | - | **done** |
| W7 | Code review and security audit: modes directory loading, seeding, key output (`key:` actions), zoom Ctrl handling across channels, systemd sandbox | `docs/security.md`, fixes in affected files | - | open |
| W8 | Keep documentation in sync with final behavior (`readme.md`, `docs/`) | `readme.md`, `docs/` | after each feature | open |
| W9 | Verify device descriptors and factory button counts on real hardware; add SpaceExplorer / SpacePilot Pro counts once verified | `devices/*.conf`, `src/device_db.cpp` | hardware | open |
| W10 | CI hardening: ASAN job, secret scan, cppcheck, package builds on tags | `.github/workflows/`, `CMakeLists.txt` | - | **done** |
| W11 | Avoid the Xlib `None` macro clash in `src/modes.hpp` (daemon currently uses `#undef None`) | `src/modes.hpp`, `src/dofscroll.cpp` | - | **done** |
| W12 | CMake parity with the Makefile (check/ASAN/hardening/install targets), then remove the Makefile | `CMakeLists.txt`, `cmake/`, `scripts/`, build sections of docs | - | **done** |
| W7a | Security review as a written report only (no code edits): modes directory loading, seeding, `key:` actions, parser limits, zoom Ctrl handling across channels, systemd sandbox. Fixes come later as separate packages | new `docs/security-review-2026-10.md` | - | open |
| W3a | UI toolkit decision per OS as a design document: Qt6 vs local web UI vs native minimal tray; effort, license fit with GPL, size, reuse of the gettext catalogs; with a recommendation | new `docs/ui-options.md` | - | open |
| W4a | Release and signing process document: Apple Developer ID and notarization, Windows code signing, secrets in GitHub Actions, tag scheme, artifacts per OS | new `docs/release-process.md` | - | open |
| W13 | `hidprobe`: small diagnostic tool that records raw HID reports of a 3D mouse and checks them against `devices/*.conf` (verifies the report layouts, prerequisite of W9); runs on Linux, Windows, macOS | new `tools/hidprobe/` | - | open |
| W14 | Repository hygiene for the later public launch: `SECURITY.md`, issue and PR templates, `dependabot.yml`, `CODEOWNERS` | new `SECURITY.md`, `.github/ISSUE_TEMPLATE/`, `.github/dependabot.yml`, `.github/CODEOWNERS` (not `.github/workflows/`) | - | open |
| W15 | Seeding race: `seedDefaults()` creates `modes/` before copying the files, so a restart in between leaves a partial set that is never completed; copy into a temporary directory and rename it, or check per file | `src/seed.cpp`, `tests/test_seed.cpp` | - | **done** |

## Decisions (2026-10-19)
- Version 0.1 on Windows and macOS ships a minimal native tray only; the large settings window (Qt6 or local web UI) is a later version (see W3a, `docs/ui-options.md`).
- Version 0.1 installers are unsigned and not notarized; signing is added later when certificates exist (`docs/release-process.md`).
- The git history of this repository is published as is.

Order that avoids conflicts: W0b touches core files and is merged first; W1/W2 start now with
backends in new files (they only depend on the interfaces from W0a) and wire the daemon `main` once
W0b is on `main`. Backends must be unit-testable without hardware (see `FakeInputSource`). W5-W11 are independent of each other.
