<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Contributing to dofscroll

Thanks for helping. This is the short version; `readme.md` is the full technical documentation
and `AGENTS.md` the orientation for coding agents.

By contributing you agree that your contribution is licensed under `GPL-3.0-or-later`, like the
rest of the project.

## Build

Dependencies (RHEL 9 + EPEL, see `readme.md` section 5 for the exact command): `gcc-c++`,
`spacenavd`, `libspnav-devel`, `libX11-devel`, `libXtst-devel`, `gtk3-devel`,
`libappindicator-gtk3-devel` (plus `cmake` 3.16+ and `gettext`). If your system has no
suitable `cmake`: `pip install --user cmake` (installs to `~/.local/bin`).

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
- Update `readme.md` when behavior or a contract (config key, file path) changes.

## English only

Everything in the repository is English: code comments, identifiers, documentation, log, error
and test messages, commit messages. The only exception is translations in `po/*.po`.

## Adding a mode

Modes are declarative files, no recompiling needed (format and limits: `readme.md` section 4,
"Modes"; parser: `src/modes.hpp`).

1. Create `<name>.conf` (the name is the id, `[a-z0-9_-]{1,32}`). For a mode that ships with the
   project put it in `examples/modes/`; for personal use put it in
   `~/.config/dofscroll/modes/`.
2. Use `NAME`, `DESCRIPTION`, `V_ACTION`/`H_ACTION`/`TWIST_ACTION`, `BUTTON_<n>` and any settings
   key as an override. Button indices must be below the factory button count of the device.
3. Select it with `MODE = <name>` in `[global]`, `[device:*]` or `[app:*]`.
4. Example modes are validated by `--target check-config`/CI; run `--target check` before sending it.
   Keep SPDX headers on shipped files.

## Adding a language

UI strings are English in the source and translated with gettext; translations live in `po/`.

1. Create `po/<lang>.po` from the translation template in `po/` (for example
   `msginit -l <lang> -i po/<template>.pot -o po/<lang>.po`; use the template file name that
   exists in `po/`).
2. Translate the entries; keep placeholders (`%s`, `%d`) and mnemonics intact.
3. Register the language if the build lists languages explicitly, rebuild and run the tray with
   `LANGUAGE=<lang>` to check it.
4. When you add or change UI strings in the source, keep them English and re-extract the
   template so translators see the new entries.

## Security

Please report security problems privately to the maintainer rather than in a public issue. The
threat model and past findings are in `docs/security.md`.
