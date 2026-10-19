# AGENTS.md

Context for AI coding agents (Claude Code, Codex, Cursor, ...) working on this repo. For the
full architecture and operating instructions see `readme.md` - this document is only a short
orientation.

## What this is

dofscroll: a small C++ driver bridge, running as two separate processes, for a 3Dconnexion
SpaceMouse on Linux (X11, tested on RHEL 9). The daemon (`src/dofscroll.cpp`) reads the mouse
via `spacenavd`/`libspnav` and turns it into X11 scroll/zoom/arrow-key/mouse-button events
(250 Hz tick rate (`TICK_HZ`), reader thread and main thread decoupled). What the channels and
buttons do is decided by a **mode** (declarative plugin files, see `readme.md` section 4). The
second component, `src/dofscroll_tray.cpp`, is a GTK3+AppIndicator tray icon with a settings
window that writes the same config file and triggers a live reload in the daemon via SIGHUP;
it also seeds example config/modes on first start. Build with `make` or CMake (see below); CI
will be a GitHub Actions workflow (planned).

## Where things are

| Path | Purpose |
|---|---|
| `src/` | Sources: `dofscroll.cpp` (daemon), `dofscroll_tray.cpp` (tray), `config`/`dsp`/`modes` (shared resp. unit-tested), `output*` (X11/uinput), `activewin`, `seed`, `device_db.hpp`; headers are the contract |
| `tests/` | Unit tests (`make test`) |
| `examples/` | Example configs: `dofscroll-pro.conf`, `-compact.conf`, `-multi-device.conf` (CI-validated), `default/` (seeded on first start), `modes/` (example modes) |
| `po/` | gettext translations (`*.po`); the only place for non-English text |
| `scripts/` | Pre-commit hook, hardening check |
| `Makefile`, `CMakeLists.txt` | Build |
| `docs/security.md`, `docs/windows-mac.md`, `packaging/` | Security review; driver research for Windows/Mac; udev rule for uinput |
| `dofscroll`, `dofscroll-tray` | Locally built binaries in the repo root (see "Warning" below) |
| `icons/dofscroll-tray.svg` | Custom tray icon |
| `systemd/dofscroll.service`, `systemd/dofscroll-tray.service` | User systemd units, linked (not copied) into `~/.config/systemd/user/` |
| `readme.md` | Full technical documentation (architecture, config keys, modes, install/move instructions) - **the single source of truth for operation/install**; this AGENTS.md does not duplicate it |
| `CONTRIBUTING.md`, `THIRD-PARTY-NOTICES.md` | Contribution guide; licenses of dependencies |

`~/.config/dofscroll/dofscroll.conf` (legacy: `~/.config/spnav-scroll/spnav-scroll.conf` and
`~/.config/spnav-scroll.conf`, migrated by the tray at start) is NOT part of the repo (purely
user-specific) - see `readme.md` section 4 for the format.

## Build (verified)

```sh
make check-deps   # check dependencies
make -j4          # both binaries in the repo root
make test         # unit tests
make check        # complete quality gate (werror, tests, ASAN, cppcheck, hardening)
```

CMake equivalent: `cmake -S . -B build && cmake --build build -j4`.

Required packages (RHEL 9 + EPEL): `gcc-c++ spacenavd libspnav-devel libX11-devel
libXtst-devel gtk3-devel libappindicator-gtk3-devel` (plus `cmake`, `gettext` for CMake and
translations) - exact `dnf install` in `readme.md` section 5.

**Warning before a rebuild:** the two binaries in the repo root are purely local build
artifacts (listed in `.gitignore`, not tracked - only the sources are) - a fresh clone does not
have them until the build above creates them. A running service keeps its binary open (`Text
file busy`) - before a rebuild on a system with active services, stop them first: `systemctl
--user stop dofscroll.service dofscroll-tray.service`, then rebuild, then `systemctl --user
start ...`.

## Verifying a change

Unit/scenario tests (`make test`) cover config, DSP, modes and the axis wizard; beyond that the
behavior depends on real SpaceMouse hardware, a running X11 session and `spacenavd`. What an
agent without hardware access can realistically do:
1. Run `make check` (at least `make check-fast`) and check for compile errors/warnings.
2. Check `readme.md` for contradictions with the changed logic (the code is the source of truth
   for behavior, the README only for architecture/contracts - on a behavior change update the
   README as well, not just the code).
3. Real verification (scrolling/zooming/nav mode/click button, `journalctl --user
   -u dofscroll.service -f`) can only be done by a human on real hardware - say so clearly
   instead of pretending it was tested.

Rules that are easy to break: the number of configurable buttons must come from
`factoryButtonCount(deviceName, reported)`, never from the reported count alone; the headers
`src/modes.hpp`, `src/keys.hpp`, `src/seed.hpp`, `src/device_db.hpp` are fixed interfaces.

## Language

All repository text is English: code comments, identifiers, documentation, log/error/test
messages, commit messages. The only exception is the German translation in `po/*.po`. New UI
strings are written in English and made translatable (gettext); do not hard-code German (or any
other language) in source files. New files carry the SPDX header
(`SPDX-License-Identifier: GPL-3.0-or-later`).
