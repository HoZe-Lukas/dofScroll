<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Prompt for the Qt6 user interface and installer sessions (Claude Code on the web)

Decision (2026-10-19): **one user interface for Linux, Windows and macOS, built with Qt6 Widgets**
(tray icon + settings window). The existing GTK3 tray stays on Linux until the Qt interface has
the same features; it is removed in a later change. Version 0.1 is released without waiting for
the Windows/macOS interface (daemons only, configuration by file), the shared interface follows in
0.2. Installers are unsigned (see `docs/work-plan.md`).

Start this session only after the Windows and macOS port pull requests are merged into `main`.
It runs on Linux, so verification comes from the GitHub runners (`windows-latest`, `macos-latest`)
and the Linux job.

```text
You are building the shared Qt6 user interface (package W3) for dofscroll on Linux, Windows and
macOS. You run on Linux in the cloud: verification = GitHub Actions runners plus unit tests that
run on Linux. Never claim behavior you did not observe; a human tests on real machines afterwards.

FIRST: `git fetch && git log --oneline origin/main | head -30`. If the Windows and macOS backends
(src/platform/win, src/platform/mac) are not on main yet, stop and tell the user.
READ: AGENTS.md, docs/work-plan.md, docs/ui-options.md and docs/release-process.md if present,
docs/port-architecture.md, docs/configuration.md, docs/modes.md, src/platform/platform.hpp,
src/dofscroll_tray.cpp (the GTK tray: it defines the features and behavior to reproduce),
src/mode_list.hpp, src/autostart.hpp, src/axis_wizard.hpp, src/common.hpp (status file),
src/i18n.hpp, po/, CMakeLists.txt, .github/workflows/.

SCOPE (feature parity with the GTK tray, one codebase for all three systems):
- Executable `dofscroll-ui`: QSystemTrayIcon with a menu (status lines, "Mode" submenu with one
  radio item per loaded mode and entries to open/create mode files and the modes folder, "Enabled"
  toggle, "Open settings", "Open config", "Open log", "Quit") and a settings window (QMainWindow)
  with the same pages and options as the GTK settings window: profile selector ([global],
  [device:*], [app:*]), channel settings (mix, sensitivity, deadzone, curve with live preview,
  smoothing, sign, zoom options), buttons (only up to the factory button count,
  factoryButtonCount()), "Application modes" list, the axis assistant (wizard), the "Start at
  login" checkbox in the settings window only (NOT in the tray menu; it switches the daemon and
  the UI together: use the contract documented in src/autostart.hpp with a Linux, a Windows and a
  macOS implementation), and the language handling.
- Keep ALL logic toolkit-free and unit-tested on Linux: status file parsing, menu model, settings
  model (reading/writing through the existing config library: atomic save, then requestReload()),
  mode list and template creation (src/mode_list.hpp: make it portable by using the file-system
  layer src/platform/fs.hpp instead of POSIX calls; tests/test_mode_list.cpp currently does not
  build on Windows), wizard evaluation (src/axis_wizard.hpp), autostart state/argv. The Qt code is
  a thin view layer.
- Live axis display and the wizard need a live data feed. On Linux the GTK tray reads libspnav
  directly; on Windows and macOS the daemon owns the device exclusively. Design a small local feed
  from the daemon to the UI (per-user socket or named pipe, user-only permissions, bounded rate,
  read-only for the UI, no commands accepted over it) and document it in docs/port-architecture.md;
  implement it as a separate step after the rest works. Until then the pages that need it say
  "not available yet" on Windows and macOS.
- Translations: English source strings, the same German catalog as the GTK tray (po/de.po). Reuse
  src/i18n.hpp style wrappers or convert .po to Qt .qm at build time (lconvert); decide, justify in
  docs/ui-options.md and keep a single catalog as the source of truth. German must be complete.
- Qt: Qt6 Widgets only (no QML, no WebEngine), dynamic linking (LGPLv3 allows it with GPL-3.0):
  add Qt to THIRD-PARTY-NOTICES.md and the installation docs (how users get the Qt libraries on
  Linux; what is bundled on Windows/macOS). CMake: option DOFSCROLL_UI (default ON when Qt6 is
  found), `find_package(Qt6 COMPONENTS Widgets REQUIRED)`, AUTOMOC; the GTK tray target stays.
- Platform specifics in small files: Windows (Startup entry or scheduled task for both programs,
  single instance, open files via QDesktopServices), macOS (LaunchAgent for both programs,
  LSUIElement menu-bar agent, no Dock icon, open files via NSWorkspace/QDesktopServices),
  Linux (systemctl --user enable/disable for dofscroll.service and dofscroll-tray.service; the
  Qt UI needs its own user unit `dofscroll-ui.service`; keep the sandbox directives, adapt only
  what Qt needs and justify each change).

UI DESIGN REQUIREMENTS (maintainer, 2026-10-19; they apply to the pages stage):
- Left: profile tree with "Default mode" ([global]) on top, then device and application profiles;
  right: the settings of the selected profile. The right side is built from a model-driven layout
  and is rebuilt when the mode of the selected profile changes (not fixed tabs).
- The default mode (`normal`) has the most settings (channels, curves, zoom, nav/click buttons,
  button bindings). Any other mode shows only what applies to it (its channel actions and button
  bindings as info, the overrides it allows) and an "Edit mode file" button.
- [Global] is the default mode only: no mode selector there, and MODE is never written to
  [global]. The mode choice exists only for device and application profiles and is a DROPDOWN
  (QComboBox, exactly one mode): first entry "Default mode" (clears MODE), then built-ins and mode
  files by display name.
- Things that are the same in every mode are always available as their own top-level entries and
  are NOT part of a profile's page: "Channels" (mix, sensitivity, deadzone, curve, smoothing, sign,
  zoom options, edited in [global]), "General" (start at login, language) and "Axes / assistant".
  The profile tree (Default mode, devices, applications) shows only what depends on the profile's
  mode. Per-profile overrides of channel values stay possible (the config format supports them):
  they live in a collapsed "Overrides for this profile" section, off by default.
- A "Modes" entry (also always available) manages the mode files in the modes directory: list of
  all modes (built-in and file) with their loader warnings, New mode (template with the syntax
  documented in comments), Duplicate (also from built-ins), Import file... (validated before it
  is copied, warnings shown first), Edit (default editor), Delete (file modes only; removing a file
  that overrides a built-in restores the built-in), Open modes folder, and a permanent hint that
  mode files can press any key and should come from trusted sources.
- Visual quality matters: clear grouping, sensible defaults, short helper texts, consistent
  spacing, keyboard navigation, light/dark theme friendly, no wall of sliders.

BRANCHES (from main, one PR each, never merge yourself, set the package status in
docs/work-plan.md to "in progress (web, <date>)" in the first commit):
1. feat/w3-ui-core: toolkit-free models + tests, portable mode_list.hpp, Qt tray and settings
   window skeleton that builds on Linux, Windows and macOS, CI jobs for the UI on all three
   runners (install Qt per runner with pinned, checksum-verified or SHA-pinned actions/packages;
   state clearly if a runner package manager is used). Linux first, then the others.
2. feat/w3-ui-pages: the pages in the order: global settings and channels, buttons, modes list
   with the editor entries, profiles ([device:*], [app:*]), autostart checkbox, language.
3. feat/w3-ui-live: the live feed and the pages that need it (axes view, wizard).
4. feat/w4-installers (after 1 and 2 are green): unsigned installers that include the Qt libraries
   (windeployqt, macdeployqt), per-user Windows installer + ZIP, macOS .app (LSUIElement) in a
   .dmg or .pkg, Linux packages gain the UI as an optional subpackage/dependency; attach all to
   the GitHub Release via the existing tag-triggered workflow and extend SHA256SUMS.txt;
   document the SmartScreen and Gatekeeper first-run steps and the macOS permissions (Input
   Monitoring, Accessibility) in docs/installation.md; pin all actions to full commit SHAs.

RULES: everything in the repository is English (German only in po/*.po); SPDX header in new
files; no secrets, personal data or hardware-modification hints; no 3Dconnexion SDK; do not edit
core files except small justified fixes listed in the PR; run
`cmake --build build --target check-fast` before every push; do not break the Linux GTK tray or
the existing tests. In every PR list what compiled on each runner, what the tests cover, what is
untested and the manual tests a human must run on real machines.
```
