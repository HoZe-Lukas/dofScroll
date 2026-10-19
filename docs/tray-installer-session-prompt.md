<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Prompt for the Windows + macOS tray and installer session (Claude Code on the web)

Start this session only after the three port pull requests (`feat/ports-core`, `feat/w1-windows`,
`feat/w2-macos`) are merged into `main`: it builds on their backends. The session runs on Linux,
so verification comes from the GitHub-hosted `windows-latest` and `macos-latest` runners.

Decisions behind it (see `docs/work-plan.md`): version 0.1 gets a **minimal native tray** on
Windows and macOS (no settings window; the settings stay in the config file, which the tray opens)
and **unsigned** installers, with the first-run warnings explained in the install guide.

```text
You are adding the minimal native tray (package W3) and the unsigned installers (package W4) for
Windows and macOS. You run on Linux in the cloud: verification = GitHub Actions runners
(windows-latest, macos-latest) plus unit tests that run on Linux. Never claim behavior you did not
observe on a runner; hardware behavior is tested by a human afterwards.

FIRST: `git fetch && git log --oneline origin/main | head -30`. If the Windows and macOS backends
(src/platform/win, src/platform/mac) are not on main yet, stop and tell the user.
READ: AGENTS.md, docs/work-plan.md, docs/port-architecture.md, docs/port-windows.md,
docs/port-macos.md, docs/installation.md, docs/releasing.md, docs/ui-options.md and
docs/release-process.md if present, src/platform/platform.hpp, src/dofscroll_tray.cpp (the Linux
tray, for behavior), src/mode_list.hpp, src/common.hpp (status file format), CMakeLists.txt,
.github/workflows/.

BRANCHES (from main, one PR each, never merge yourself): feat/w3-tray-win, feat/w3-tray-mac,
feat/w4-installers (after both tray branches are green). Set the package status in
docs/work-plan.md to "in progress (web, <date>)" in the first commit.

W3 TRAY (separate process `dofscroll-tray` per OS, like on Linux; no settings window):
- Pure logic first, in a header that compiles and is unit-tested on Linux (src/ui/tray_model.hpp or
  similar): parse the daemon's status file (profile, device, app, mode, enabled, paused), build a
  menu model: status lines, "Mode" submenu with one radio item per loaded mode (use loadModes and
  the logic in src/mode_list.hpp; choosing `normal` clears MODE), "Enabled" toggle (writes
  ENABLED in [global]), "Start at login" (checkable; Windows: Startup entry or scheduled task at
  logon, macOS: LaunchAgent; use the same contract as src/autostart.hpp from the Linux tray: query
  the state, set enabled; use the autostart files from the W1/W2 packaging work and never run
  scripts through a shell string), "Open config", "Open modes folder", "Open log", "Quit". Changes go through the config library
  (atomic save) and then requestReload() from src/platform/platform.hpp.
- Windows: Win32 Shell_NotifyIcon + popup menu in src/ui/win/, no console window, single instance
  (named mutex), icon from icons/ (add a .ico/.rc built in CMake from the SVG if needed, or draw a
  simple icon; document how it was made). macOS: NSStatusItem + NSMenu in src/ui/mac/
  (Objective-C++), menu bar agent without Dock icon (LSUIElement), opens files with NSWorkspace.
- Strings: English wrapped like the Linux tray (see src/i18n.hpp) with German entries in po/; use
  the same gettext catalogs if gettext is available on the runner, otherwise the no-op fallback.
- CMake: src/ui/win and src/ui/mac only on their OS (cmake/windows.cmake, cmake/macos.cmake);
  ports.yml jobs must build the tray and run its tests.

W4 INSTALLERS (unsigned, version 0.1):
- Windows: a per-user installer that needs no administrator rights if possible (NSIS or WiX: choose
  one, justify it in docs/release-process.md; it must build on the windows-latest runner): installs
  dofscroll.exe, dofscroll-tray.exe, examples, licenses (GPL text and THIRD-PARTY-NOTICES.md) into
  %LOCALAPPDATA%\Programs\dofscroll, optional autostart (use the autostart mechanism from the W1
  work), uninstaller, Start menu entry. Also a portable ZIP.
- macOS: dofscroll-tray as an .app bundle (Info.plist with LSUIElement), the daemon inside or next
  to it, LaunchAgent plist from the W2 work, packaged as .dmg or .pkg; unsigned and not notarized:
  document the Gatekeeper first-run steps (right click > Open, or removing the quarantine
  attribute) and the permissions to grant (Input Monitoring, Accessibility) exactly.
- Release workflow: extend the tag-triggered jobs in .github/workflows/ci.yml (or a new
  release workflow that the existing `release` job depends on) so Windows and macOS packages are
  built on their runners and attached to the same GitHub Release with SHA256SUMS.txt covering all
  files. Pin all actions to full commit SHAs. Do not break the existing RPM/DEB/TGZ jobs.
- docs/installation.md: add real Windows and macOS sections (download, SmartScreen "More info >
  Run anyway", Gatekeeper steps, permissions, uninstall); docs/release-process.md: how the
  unsigned packages are built and how signing would be added later (certificate, notarization) as
  a short outlook, no secrets.

RULES: everything in the repository is English (German only in po/*.po); SPDX header in new files;
no secrets, personal data or hardware-modification hints; no 3Dconnexion SDK; do not edit core
files except small justified fixes listed in the PR; run `cmake --build build --target check-fast`
before every push. In every PR list what compiled on the runner, what the tests cover, what is
untested and the exact manual tests a human must run on a real Windows/macOS machine.
```
