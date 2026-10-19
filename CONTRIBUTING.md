<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Contributing to dofscroll

Thanks for helping. This is the short version. User documentation is in `readme.md` and `docs/`;
`AGENTS.md` is the orientation for coding agents.

By contributing you agree that your contribution is licensed under `GPL-3.0-or-later`, like the
rest of the project.

## Build

Dependencies (RHEL 9 + EPEL; exact commands per distribution in `docs/installation.md`,
section 4): `gcc-c++`, `spacenavd`, `libspnav-devel`, `libX11-devel`, `libXtst-devel`,
`gtk3-devel`, `libappindicator-gtk3-devel` (plus `cmake` 3.16+ and `gettext`). If your system has
no suitable `cmake`: `pip install --user cmake` (installs to `~/.local/bin`).

    cmake -S . -B build
    cmake --build build --target check-deps
    cmake --build build -j4               # binaries in build/

## Test

    ctest --test-dir build --output-on-failure   # unit and scenario tests (tests/test_*.cpp, plain assert)
    cmake --build build --target check-fast      # quick gate (also run by the pre-commit hook)
    cmake --build build --target check           # full gate: -Werror, tests, ASAN/UBSAN, cppcheck, hardening

Install the pre-commit hook once with `scripts/install-hooks.sh`. Behavior that depends on real
SpaceMouse hardware, a live X11 session or `spacenavd` cannot be covered by the tests; say in
your change description what you verified and what you could not.

New logic should be a small, pure, testable function with a unit test in `tests/` (CMake
picks up `tests/test_*.cpp` automatically; re-run cmake after adding one).

## Style

- C++17, match the surrounding code (naming, comment density, formatting).
- Headers in `src/*.hpp` are the contract between modules; do not change the signature of the
  fixed interfaces (`src/modes.hpp`, `src/keys.hpp`, `src/seed.hpp`, `src/device_db.hpp`)
  without discussing it first.
- The number of configurable buttons must come from `factoryButtonCount(deviceName, reported)`,
  never from the reported count alone.
- New files start with the SPDX header (`// SPDX-License-Identifier: GPL-3.0-or-later` and
  `// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka`; use `#` comments for CMake, shell,
  config and YAML files).
- Keep the hardening flags; the binaries must pass `scripts/check-hardening.sh`.
- Update the docs when behavior or a contract (config key, file path) changes:
  `docs/configuration.md`, `docs/modes.md`, `docs/installation.md` or `docs/troubleshooting.md`.

## English only

All files are UTF-8 without BOM (`scripts/check-utf8.sh`; PowerShell scripts stay ASCII). Text handling rules: `docs/encoding.md`.

Everything in the repository is English: code comments, identifiers, documentation, log, error
and test messages, commit messages. The only exception is translations in `po/*.po`.

This is enforced by `scripts/check-english.sh` (part of the `lint` job): it fails on German
characters and unmistakably German words outside `po/`. Run it before you push.

## Branches and pull requests

1. Branch from `main` and give the branch a topic name.
2. Keep a change small and focused. Add or update tests.
3. Run `cmake --build build --target check`. The pre-commit hook runs `check-fast`; do not skip
   it with `--no-verify`.
4. Push the branch and open a pull request against `main`. State what you verified and what
   needs real hardware. CI (`.github/workflows/ci.yml`) must be green.
5. Releases are made by the maintainer, see `docs/releasing.md`.

## Feature proposals

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. Proposals that only re-implement driver button mapping (always the same key or click) will not be accepted; features that act on the axes, modes or profiles, or bindings that depend on the active mode, are welcome.

## Adding a mode

Modes are declarative files, no recompiling needed (format and limits: `docs/modes.md`;
parser: `src/modes.hpp`).

1. Create `<name>.conf` (the name is the id, `[a-z0-9_-]{1,32}`). For a mode that ships with the
   project put it in `examples/modes/`; for personal use put it in
   `~/.config/dofscroll/modes/`.
2. Use `NAME`, `DESCRIPTION`, `V_ACTION`/`H_ACTION`/`TWIST_ACTION`, `BUTTON_<n>` and any settings
   key as an override. Buttons are numbered from 1 and must not exceed the factory button count
   of the device.
3. Select it with `MODE = <name>` in `[global]`, `[device:*]` or `[app:*]`.
4. Example modes are validated by `--target check-config`/CI; run `--target check` before sending it.
   Keep SPDX headers on shipped files.

## Adding a language

UI strings are English in the source and translated with gettext; translations live in `po/`.

1. Create `po/<lang>.po` from the template:
   `msginit -l <lang> -i po/dofscroll.pot -o po/<lang>.po`.
2. Translate the entries; keep placeholders (`%s`, `%d`) and mnemonics intact.
3. Add `<lang>` to `po/LINGUAS`. Rebuild: CMake compiles every `po/*.po` into
   `share/locale/<lang>/LC_MESSAGES/dofscroll.mo` (needs `gettext`). Check it by running the tray
   with `LANGUAGE=<lang>` and `DOFSCROLL_LOCALEDIR=build/locale`.
4. When you add or change UI strings in the source, keep them English and re-extract the
   template so translators see the new entries.

## Code of conduct

Be kind and respectful; see `CODE_OF_CONDUCT.md`.

## Security

Please report security problems privately to the maintainer rather than in a public issue. The
threat model and past findings are in `docs/security.md`.
