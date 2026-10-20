# AGENTS.md

Orientation for AI coding agents (Claude Code, Codex, Cursor, ...) working on this repo.
User documentation is in `readme.md` and `docs/`; this file only gives the short version.

## What this is

dofscroll: a C++17 bridge for a 3Dconnexion SpaceMouse, two processes. The daemon
(`src/dofscroll.cpp`) reads the device through `spacenavd`/`libspnav` and produces scroll, zoom,
key and mouse-button events through X11/XTest or uinput. The Qt6 interface
(`src/ui/`, `dofscroll-ui`, tray icon and settings window) writes the same config file and triggers a
live reload with SIGHUP, the same on all three systems. Windows and macOS daemons are built by CI,
not run on hardware (`docs/work-plan.md`).

## Layout

| Path | Purpose |
|---|---|
| `src/` | Sources (UI: `src/ui/model` toolkit-free, `src/ui/qt` Qt6 views). Headers are the contract. Shared logic: `config`, `dsp`, `modes`, `seed`, `device_db`; output: `output*`; platform code in `src/platform/` |
| `tests/` | Unit and scenario tests (`tests/test_*.cpp`, plain `assert`) |
| `examples/` | Example configs (CI-validated), `default/` (seeded on first start), `modes/` |
| `devices/` | Device descriptions, format in `docs/devices.md` |
| `po/` | gettext catalogs. The only place for non-English text |
| `systemd/`, `packaging/`, `icons/` | Unit templates (`*.service.in`, rendered by CMake), udev rule and CPack config, tray icon |
| `cmake/`, `CMakeLists.txt` | Build, quality gates, install and helper targets |
| `scripts/` | Pre-commit hook, secret scan, hardening check |
| `docs/` | `installation`, `configuration`, `modes`, `ui`, `troubleshooting` (user docs, plus `release-notes-*`); `security`, `releasing`, `work-plan`, `port-*`, `windows-mac`, `devices` |

## Build and test

    cmake -S . -B build
    cmake --build build --target check-deps     # missing dependencies
    cmake --build build -j4                     # binaries in build/
    ctest --test-dir build --output-on-failure  # tests
    cmake --build build --target check-fast     # -Werror build + tests (pre-commit gate)
    cmake --build build --target check          # + ASan/UBSan, cppcheck, hardening

If `cmake` is missing or too old: `pip install --user cmake`. A running service keeps its binary
open (`Text file busy`); stop it before rebuilding.

## Rules

- Text is UTF-8 everywhere (BOM read, never written, LF written); case-insensitive matching folds ASCII only; never use `isalpha`/`tolower` on UTF-8 bytes (use `src/utf8.hpp`); see `docs/encoding.md`.

- English everywhere (code, comments, docs, logs, commits). Only `po/*.po` has other languages.
  UI strings are English in the source and translated through gettext.
- New files carry the SPDX header (`GPL-3.0-or-later`, copyright line as in existing files).
- The number of configurable buttons comes from `factoryButtonCount(deviceName, reported)`, never
  from the reported count alone. `BUTTON_<n>` is 1-based and at most 32.
- Fixed interfaces: `src/modes.hpp`, `src/keys.hpp`, `src/seed.hpp`, `src/device_db.hpp`.
- Precedence of profiles: global < device < mode overrides < app.
- On a behavior change update the matching doc (`docs/configuration.md`, `docs/modes.md`,
  `docs/installation.md`, `docs/troubleshooting.md`) in the same change.
- Never use `--no-verify`. Do not run services, `sudo` or `git push` unless asked.
- No personal data, hostnames or employer names in the repo.
- dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. Do not add features that only re-implement driver button mapping (a button that always sends the same key or click); mode-dependent bindings, mode switching and `nav` are fine.

## Verifying a change

Tests cover config, DSP, modes and the axis wizard. Real behavior needs hardware, a live X11 or
Wayland session and `spacenavd`. Run `check-fast` at least, and say clearly what only a human can
verify on real hardware (scrolling, zoom, buttons, tray). Do not claim it was tested.
