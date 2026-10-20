<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Development

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Authoritative: [CONTRIBUTING.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/CONTRIBUTING.md) and [AGENTS.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/AGENTS.md). By contributing you agree that your contribution is licensed under `GPL-3.0-or-later`.

## Build and test

```sh
cmake -S . -B build
cmake --build build --target check-deps
cmake --build build -j4
ctest --test-dir build --output-on-failure    # tests/test_*.cpp, plain assert
cmake --build build --target check-fast       # -Werror build and tests (pre-commit hook)
cmake --build build --target check            # adds ASAN/UBSAN, cppcheck, hardening check
```

Other targets: `check-config` (validates example configs), `lint`, `install-user-links`, `install-udev`, `uninstall`. Install the pre-commit hook once with `scripts/install-hooks.sh`; do not skip it with `--no-verify`. Behavior that depends on real hardware, a live X11 session or `spacenavd` cannot be covered by tests: say in your pull request what you verified and what you could not.

## CI gates

CI ([.github/workflows/ci.yml](https://github.com/HoZe-Lukas/dofScroll/blob/main/.github/workflows/ci.yml)) builds with `-Werror` and runs tests, an ASAN/UBSAN build, cppcheck and the hardening check, and on `v*` tags builds RPM, DEB and TGZ packages. Further workflows: `lint.yml` (actionlint, shellcheck, SPDX headers), `ports.yml` (Windows and macOS builds and tests), `ui.yml` (Qt interface), CodeQL and dependency review. Third-party actions are pinned to full commit SHAs.

## Branch and pull request flow

1. Branch from `main` with a topic name (work packages use `feat/<package-id>`, see [Roadmap](Roadmap)).
2. Keep the change small; add or update tests.
3. Run `check` locally.
4. Open a pull request against `main` and state what you verified. CI must be green.
5. Releases are made by the maintainer ([docs/releasing.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/releasing.md)).

## Coding rules

- C++17; match the surrounding code. Headers in `src/*.hpp` are contracts; do not change `modes.hpp`, `keys.hpp`, `seed.hpp` or `device_db.hpp` signatures without discussion.
- Button counts come from `factoryButtonCount(deviceName, reported)`, never from the reported count alone.
- New files start with an SPDX header; keep the hardening flags.
- Everything in the repository is English; the only exception is `po/*.po`.
- No personal data or hostnames in files. Never claim real-device behavior was tested if it was not.
- Update the docs when behavior or a contract changes (`docs/configuration.md`, `docs/modes.md`, `docs/installation.md`, `docs/troubleshooting.md`).

## Add a joystick mode

Create `<name>.conf` (id `[a-z0-9_-]{1,32}`) in `examples/modes/` for a shipped (built-in, embedded at build time) mode or in `~/.config/dofscroll/modes/` for personal use. Use `NAME`, `DESCRIPTION`, `V_ACTION`/`H_ACTION`/`TWIST_ACTION`, `BUTTON_<n>` (from 1, within the factory count) and settings overrides. Shipped modes are validated by `check-config` and CI and need SPDX headers and the safety note. See [Modes](Modes).

## Add a device description

Add `devices/<name>.conf` with `name`, `id`, optional `factory_buttons` and the `axis.*`/`buttons` lines (format on [Devices](Devices)). Verify the layout with `dofscroll-hidprobe --check` and `--guided` on the real device before claiming it works, and add the factory count to `src/device_db.cpp` only with a verifiable source.

## Add a language

Create `po/<lang>.po` with `msginit -l <lang> -i po/dofscroll.pot -o po/<lang>.po`, translate, keep placeholders, add `<lang>` to `po/LINGUAS`, rebuild (needs `gettext`) and test with `LANGUAGE=<lang> DOFSCROLL_LOCALEDIR=build/locale`. UI strings stay English in the source.

See also [Architecture](Architecture).
