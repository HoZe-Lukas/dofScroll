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
| W2 | macOS port: IOHID `InputSource`, CGEvent `Output`, `NSWorkspace` `ActiveWindowProvider`, paths, `ReloadListener`, LaunchAgent, permissions | new `src/platform/mac/`, `cmake/macos.cmake`, `packaging/macos/` | W0a done; the final daemon `main` waits for W0b | **implemented, CI-verified; hardware-unverified** (build incl. Objective-C++, unit tests, LaunchAgent plist lint/round trip on `macos-latest`; manual tests: `docs/port-macos-checklist.md`) |
| W3 | One Qt6 user interface (tray + settings window) for Linux, Windows and macOS with feature parity to the GTK tray; GTK tray stays on Linux until parity; prompt in `docs/ui-qt-session-prompt.md` | new `src/ui/`, `po/`, CMake UI parts, `docs/ui-options.md` | W1, W2 merged | open (starts after the port PRs are merged) |
| W4 | Unsigned installers including the Qt libraries: Windows per-user installer + ZIP, macOS .app/.dmg(.pkg), Linux UI subpackage; attach to the GitHub Release; Gatekeeper/SmartScreen steps in `docs/installation.md` | `packaging/windows/`, `packaging/macos/`, `.github/workflows/`, `docs/installation.md`, `docs/release-process.md` | W3 (core + pages) | open (same session as W3) |
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
- One user interface for all three systems, built with Qt6 Widgets (decision 2026-10-19); version 0.1 is released without waiting for it (Windows/macOS daemons configured by file), the shared interface follows in 0.2. The GTK tray stays on Linux until the Qt interface has parity.
- Version 0.1 installers are unsigned and not notarized; signing is added later when certificates exist (`docs/release-process.md`).
- The git history of this repository is published as is.

## Parity rule and matrix (maintainer decision, 2026-10-19)

Every user-visible feature must work on Linux, Windows and macOS. A feature is "done" only when
all three have an implementation behind the same interface and the same tests (platform parts
compile on their runner). Toolkit-free logic is shared; platform parts are small.

| Feature | Shared logic | Linux | Windows | macOS |
|---|---|---|---|---|
| Daemon: input, output, reload, paths | `daemon_core` | done | PR #3 | PR #4 |
| Modes (files, `mode:` actions, loader warnings) | `modes`, `mode_runner` | done | via core | via core |
| Active window / application profiles | `profile_match` | done | PR #3 | PR #4 |
| Settings UI (Qt6) | `src/ui/model/` | W3 (GTK tray exists) | W3 | W3 |
| Mode management in the UI (new, duplicate, import, edit) | ModeCatalog | W3 | W3 | W3 |
| Running-program picker + capture next focus | `app_picker_model` | PR `feat/w3-app-picker` | **W17 (open)** | **W17 (open)** |
| Start at login (daemon + UI) | `autostart.hpp` contract | GTK done, Qt W3 | W18 (open) | W18 (open) |
| Open file/folder/log in the default app | `ActionRunner` | W3 | W3 | W3 |
| Live axes view and wizard (data feed from the daemon) | model | W3 (stage 3) | W3 (stage 3) | W3 (stage 3) |
| Installers/packages | CPack/CI | RPM, DEB, TGZ done | W4 | W4 |

Open packages created by this rule:
| Id | Package | Files | Depends on | Status |
|---|---|---|---|---|
| W17 | Running-program picker for Windows (`EnumWindows` + process executable name, same string as the foreground provider) and macOS (`NSWorkspace` runningApplications, regular apps, bundle id or name as the foreground provider reports), plus the device list from the HID backends | `src/platform/win/running_apps_win.cpp`, `src/platform/mac/running_apps_mac.mm`, tests, CMake hooks in `cmake/windows.cmake`, `cmake/macos.cmake` | picker PR and port PRs merged | open |
| W18 | Start-at-login implementations behind `src/autostart.hpp`: Windows (Startup entry or scheduled task for daemon and UI), macOS (LaunchAgents for daemon and UI), Linux Qt UI unit | `src/platform/win/`, `src/platform/mac/`, `src/platform/linux/` autostart files, packaging | W3 core, port PRs merged | open |

Order that avoids conflicts: W0b touches core files and is merged first; W1/W2 start now with
backends in new files (they only depend on the interfaces from W0a) and wire the daemon `main` once
W0b is on `main`. Backends must be unit-testable without hardware (see `FakeInputSource`). W5-W11 are independent of each other.
