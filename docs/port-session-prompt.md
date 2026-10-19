<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Prompt for the Windows + macOS port session (Claude Code on the web)

Start one Claude Code web session on this repository (branch `main`) and paste the block below.
The session runs on Linux, so it cannot execute Windows or macOS binaries: verification comes from
the GitHub-hosted runners (`windows-latest`, `macos-latest`) via a new CI workflow, plus unit tests
that run on Linux. Hardware tests are done afterwards by a human.

```text
You are porting dofscroll to Windows (package W1) AND macOS (package W2) in one session.
You run on Linux in the cloud: you cannot run Windows/macOS programs. Verification = GitHub Actions
runners (windows-latest, macos-latest) plus unit tests that run on Linux. Never claim hardware
behavior you did not observe.

READ FIRST: AGENTS.md, docs/work-plan.md, docs/port-architecture.md, docs/port-windows.md,
docs/port-macos.md, docs/windows-mac.md, docs/devices.md, src/platform/platform.hpp,
src/daemon_core.hpp, src/platform/linux/ (reference implementation of every seam),
src/hid_descriptor.hpp, devices/*.conf, CMakeLists.txt.

BRANCHES (all from main, one PR each, never merge yourself; open the PRs in this order):
  1. feat/ports-core    : make the shared core build and pass `ctest` on Windows and macOS, plus CI.
  2. feat/w1-windows    : branched from feat/ports-core. Windows backends.
  3. feat/w2-macos      : branched from feat/ports-core. macOS backends.
Set the status of W1/W2 in docs/work-plan.md to "in progress (web, <date>)" in the first commit of
their branches. You may run one subagent per OS for steps 2 and 3 once step 1 is green.

STEP 1: feat/ports-core
- The core uses POSIX file APIs (open/O_NOFOLLOW/lstat/getuid/mkdtemp/readdir, /proc/self/exe) in
  src/config.cpp, src/modes.cpp, src/seed.cpp, src/daemon_core.cpp and several tests. Put the
  OS-specific bits behind a small file-system layer (e.g. src/platform/fs.hpp + one implementation
  per OS family) or #ifdef, so the core compiles on Windows (MSVC and mingw) and macOS. You may edit
  those core files for this purpose only; Linux behavior must stay bit-identical and all existing
  tests must pass unchanged on Linux. Security properties must be kept: no symlink following where
  the Linux code refuses it, owner/permission checks where applicable (use the nearest OS
  equivalent and document gaps honestly in docs/security.md).
- Add .github/workflows/ports.yml (new file; do not edit ci.yml): jobs on windows-latest and
  macos-latest that configure with CMake, build the core + tests (tray and Linux backends are
  excluded there) and run ctest. Pin actions to full commit SHAs, `permissions: contents: read`.
- Push and iterate until the Linux job and both new jobs are green; read the run results with the
  GitHub tools you have. If you cannot read CI results, say so and mark everything unverified.

STEPS 2 and 3: backends in NEW files only
- InputSource: raw HID, no vendor SDK ever. Windows: hid.dll/SetupAPI (or RawInput); macOS:
  IOHIDManager. Decode with src/hid_descriptor.* and devices/*.conf; one DeviceInfo per physical
  device; hot-plug; document how the vendor driver holding the device behaves.
- Output: Windows SendInput (wheel, hwheel, every Key in src/keys.hpp, mouse buttons 1-3); macOS
  CGEventPost. Put key tables in headers that compile and are unit-tested on Linux (like
  src/key_tables.hpp). releaseAll/forceReleaseAll must release everything held.
- ActiveWindowProvider: Windows foreground window -> process executable name; macOS NSWorkspace
  frontmost app (Objective-C++; bundle identifier or name).
- platformPaths(): Windows %APPDATA%\dofscroll (config) and %LOCALAPPDATA%\dofscroll (runtime);
  macOS ~/Library/Application Support/dofscroll and ~/Library/Caches/dofscroll; user-only access.
- ReloadListener + requestReload(): Windows named event; macOS distributed notification or a
  user-only Unix socket; contract in platform.hpp.
- A thin `main` wiring these into the daemon core exactly like src/dofscroll.cpp does for Linux;
  Windows: no console window; autostart (Windows Startup entry or scheduled task at logon, macOS
  LaunchAgent plist) as files under packaging/<windows|macos>/; log and status files as in
  docs/port-architecture.md.
- CMake: cmake/windows.cmake and cmake/macos.cmake, selected by WIN32/APPLE; extend ports.yml so the
  daemon binary builds on the runner. Keep every pure-logic part (path building from an environment
  map, report decoding glue, key tables, reload contract) testable on Linux with unit tests;
  platform calls stay thin.
- Tray/settings UI is NOT part of this task (package W3).

RULES: everything in the repository is English; SPDX header in new files; no secrets or personal
data; no 3Dconnexion SDK; do not edit core files in steps 2 and 3 except small justified fixes that
you list in the PR. Run `cmake --build build --target check-fast` before every push (install the
hook with scripts/install-hooks.sh).

FINISH: for each PR write a checklist: what compiled on the runner, what the unit tests cover, what
is untested, and the exact manual hardware tests a human must run (device detection, motion,
buttons, scroll/zoom output, per-app profile switching, autostart, reload). Update the W1/W2 rows
of docs/work-plan.md to their real state.
```
