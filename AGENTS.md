# AGENTS.md

Orientation for AI coding agents (Claude Code, Codex, Cursor, ...) working on this repo.
User documentation is in `readme.md` and `docs/`; this file only gives the short version.

## What this is

dofscroll: a C++17 bridge for a 3Dconnexion SpaceMouse, two processes. The daemon
(`src/dofscroll.cpp`) reads the device through `spacenavd`/`libspnav` and produces scroll, zoom,
key and mouse-button events through X11/XTest or uinput. The Qt6 interface
(`src/ui/`, `dofscroll-ui`, tray icon and settings window) writes the configuration (`dofscroll.conf` for `[global]`,
one file per profile in `devices/` and `apps/`; the daemon only reads and never migrates) and triggers a
live reload: SIGHUP on Linux, the reload socket on macOS (there SIGHUP quits the daemon), a named event on Windows. Windows and macOS daemons are built by CI,
not run on hardware (`docs/work-plan.md`).

## Layout

| Path | Purpose |
|---|---|
| `src/` | Sources (UI: `src/ui/model` toolkit-free, `src/ui/qt` Qt6 views). Headers are the contract. Shared logic: `config`, `dsp`, `modes`, `seed`, `device_db`, `key_engine`, `pointer_engine`, `macro`, `profile_files`; output: `output*`; platform code in `src/platform/` (Linux reload by process id: `src/platform/linux/reload_fallback.hpp`). In `src/ui/model`: `part_tuning*.hpp` writes the independent tuning of a push or tilt in a profile (`SCROLL_<V|H>_<PUSH|TILT>_*`, splits the channel on the first edit) |
| `tests/` | Unit and scenario tests (`tests/test_*.cpp`, plain `assert`) |
| `examples/` | Example configs (CI-validated), `default/` (seeded on first start), `split/` (one file per profile), `modes/` and `buttons/` (built into the program) |
| `devices/` | Device descriptions, format in `docs/devices.md` |
| `po/` | gettext catalogs. The only place for non-English text, apart from the German website `site/de/` |
| `site/` | Static website (GitHub Pages, `.github/workflows/pages.yml`): English at the root, German in `site/de/`; checked by `scripts/check-site.sh`, see `docs/discoverability.md` |
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

- UI: key names are shown translated (`StringProvider::keyLabel`, gettext context `key`, `tests/test_ui_key_names.cpp`); files, config text and the parser keep the English names. No Save, Revert or "unsaved changes" question anywhere. Every edit goes into the model and is written by the autosave (`UiContext::saveSoon()` / `saveTyped()`, `src/ui/model/autosave.hpp`); an invalid value is never written and is shown next to its field. The two kinds of modes are called "Joystick mode" and "Button mode" in the UI (never plain "mode"; `src/ui/model/ui_terms.hpp`). Inheritance (Global < device < joystick mode overrides < program) is always shown in words.

- English everywhere (code, comments, docs, logs, commits). Only `po/*.po` has other languages. One documented exception: the website under `site/de/` is German (the maintainer's decision, so German users find the project); `site/` (English) and `docs/discoverability.md` stay English, and `scripts/check-english.sh` stays strict everywhere else. `scripts/check-site.sh` keeps both languages in parity.
  UI strings are English in the source and translated through gettext.
- New files carry the SPDX header (`GPL-3.0-or-later`, copyright line as in existing files).
- The number of configurable buttons comes from `factoryButtonCount(deviceName, reported)`, never
  from the reported count alone. `BUTTON_<n>` is 1-based and at most 32.
- Fixed interfaces: `src/modes.hpp`, `src/keys.hpp`, `src/seed.hpp`, `src/device_db.hpp`, `src/macro.hpp` (macro grammar and limits; the daemon and the New function dialog use the same parser).
- The Qt version of every package that ships Qt (Linux, Windows, macOS) is set only in `packaging/qt.env`; never
  hard-code it in a workflow or a script. The Linux packages bundle Qt (`scripts/bundle-qt-linux.sh`,
  `docs/release-process.md`): new bundled libraries need their license text in the package.
- Precedence of profiles: global < device < mode overrides < app. Profiles of one kind are applied by `PRIORITY` ascending (`src/profile_files.hpp`, `docs/plan-config-split.md`); a program's `BUTTON_<n>` beats the joystick mode's binding.
- A UI change brings the screenshots that show it up to date in the same change (`scripts/make-site-screenshots.sh` for `site/img/`, plus any wiki or docs images) and their captions.
- On a behavior change update the matching doc (`docs/configuration.md`, `docs/modes.md`,
  `docs/installation.md`, `docs/troubleshooting.md`) in the same change.
- Build inputs are pinned for stability but kept at the newest versions: image digests (`scripts/pin-ci-images.sh`), the hash-pinned aqtinstall requirements (`scripts/update-aqt-requirements.sh`) and the action SHAs (Dependabot) are refreshed by the monthly `refresh-ci-pins` workflow (start it by hand before a release), which opens the PR "Refresh CI pins" (never auto-merged; the job approves its checks itself). The image digests are in `packaging/ci/images.env`, not in the workflows. That PR is merged before a release, so the release uses the newest tested versions; `scripts/check-ci-pins.sh` guards the pins. Never leave the pins stale at a release, and never hand-edit the generated files.
- Every release (`docs/releasing.md`): before the tag, bring the wiki (`docs/wiki/`), the website (`site/`, English and German), `readme.md` and the other READMEs (`packaging/*/README*`) and the agent files (`AGENTS.md`) up to date and check their statements (tested platforms, versions, known limitations, features) against the code and the release notes; after the tag, check every open issue against the release (comment what changed, close only what is fully fixed and needs no hardware check). The release is not finished before that is done. The by-hand steps of the maintainer for a release are in `docs/release-checklist-<version>.md` (linked from `docs/releasing.md`).
- German texts (`po/de.po`, `site/de/`) use the informal "du", never "Sie" (maintainer decision); `scripts/check-german-du.sh` (ctest `check_german_du`) guards the common formal forms.
- Never use `--no-verify`. Do not run services, `sudo` or `git push` unless asked.
- No personal data, hostnames or employer names in the repo.
- dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. Do not add features that only re-implement driver button mapping (a button that always sends the same key or click); mode-dependent bindings, mode switching and `nav` are fine.

## Verifying a change

Tests cover config, DSP, modes and the axis wizard. Real behavior needs hardware, a live X11 or
Wayland session and `spacenavd`. Run `check-fast` at least, and say clearly what only a human can
verify on real hardware (scrolling, zoom, buttons, tray). Do not claim it was tested.
