<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# User interface options: decision record

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

Status: decided 2026-10-19 (see `docs/ui-qt-session-prompt.md`). The interface is experimental
(`dofscroll-ui`, skeleton only); the GTK tray stayed on Linux until the Qt interface had the same
features and was removed in 0.1.1 (#43).

## Decision

One user interface for Linux, Windows and macOS: **Qt6 Widgets** (tray icon + settings window),
dynamically linked. No QML, no WebEngine.

## Options considered

| Option | For | Against |
|---|---|---|
| Qt6 Widgets (chosen) | One codebase and one tray API (`QSystemTrayIcon`) on all three systems; native look; LGPL-3.0 dynamic linking is compatible with GPL-3.0; the logic stays toolkit-free, Qt is a thin view layer | Large runtime on Windows/macOS (tens of MB bundled via `windeployqt`/`macdeployqt`); a new build dependency; Linux needs Qt6 libraries installed |
| Local web UI (daemon serves a page, browser as view) | No toolkit, small | A listening socket is a new attack surface for a daemon that injects input; no real tray icon, so a native shim is needed on each system anyway; session/auth handling; worse offline/first-run experience |
| GTK everywhere | Existing tray code (GTK3 + AppIndicator) | GTK on Windows/macOS needs a bundled runtime and looks foreign; AppIndicator exists only on Linux; the tray would still need per-OS code |

Effort: Qt costs one rewrite of the settings window, but removes three separate UI efforts. The
GTK tray kept working during the migration and has been removed since (0.1.1).

## Translation decision

**po/de.po is converted to a Qt catalog (`dofscroll_de.qm`) at build time with `lconvert`**
(post-build step in `cmake/Ui.cmake`); `po/` stays the single source of truth.

Why not the gettext wrappers of `src/i18n.hpp`: they need `<libintl.h>`, which MSVC and the
macOS SDK do not provide (the header falls back to English there), so German would be missing on
two of three systems. `QTranslator` works identically everywhere. The Qt UI uses English source
strings as keys through `T("...")` (`QCoreApplication::translate` with an empty context), which
matches the msgids in the existing catalog.

Availability of `lconvert`: Linux (apt `qt6-tools-dev-tools`) yes; the aqtinstall base install
on Windows/macOS does not include it, so the CI jobs there build without the catalog and the UI
falls back to English. Installers (later package) must bundle the `.qm` produced on a Linux
build or install the Linguist tools. The build never fails because of a missing `lconvert`.

## How Qt is obtained

| OS | Development / CI | Users |
|---|---|---|
| Linux | distribution packages (`qt6-base-dev`, `qt6-tools-dev-tools`, `libqt6svg6-dev` on Debian/Ubuntu; `qt6-qtbase-devel`, `qt6-qtsvg-devel`, `qt6-linguist` on Fedora/EPEL 9 family if available) | the distribution's Qt6 libraries (dynamic) |
| Windows | `jurplel/install-qt-action` (SHA-pinned) with a pinned Qt version (aqtinstall), MSVC | Qt DLLs bundled next to the program (`windeployqt`, later installer package) |
| macOS | same action, pinned Qt version | Qt frameworks bundled in the app (`macdeployqt`, later installer package) |

The EPEL/Rocky availability of Qt6 was not verified; CI uses `ubuntu-latest` for Linux.

## Skeleton

## Window structure

A `QSplitter`: navigation tree on the far left, a stacked settings area on the right.

- Always-available top-level entries (identical in every mode): **Channels** (mix, sensitivity,
  deadzone, curve, smoothing, sign, zoom options), **General** (start at login, language) and
  **Axes / assistant**.
- **Modes** (mode management, also mode-independent): a list of all modes (built-in and file
  modes, warnings shown per mode) with the buttons New mode, Duplicate, Import file..., Edit
  (opens the file in the default editor), Delete (file modes only, with confirmation) and Open
  modes folder, plus a permanent hint that mode files can press any key and should only come
  from trusted sources. A thin placeholder for now; the logic comes from the `ModeCatalog` model.
- A **Profiles** group below them: "Global" (the `[global]` section) at the top, then the
  device and application profiles. Only what depends on the profile's mode lives here.
- The profile editor has the same tabs for every profile (General, Channels, Buttons, Motions and buttons, Overrides) and
  keeps the selected tab when another profile is chosen; only the content is rebuilt when the
  selected profile or its effective joystick mode changes. Global has a joystick mode drop-down too
  (without "Same as Global"); device and program profiles start theirs with "Same as Global" and get
  an "Edit joystick mode file" button for a joystick mode with a file.
- Channel values are the Channels tab of the profile (Global included); a device or program profile
  shows the inherited value with its origin and overrides it per row. There is no Channels page.
- There is no Save button anywhere: every change is saved by itself (see ui.md, "Saving").

## Skeleton

`dofscroll-ui`: tray icon with placeholder menu (Open settings, Quit), the window above with
placeholder pages and three placeholder profile entries, a single-instance guard
(`QLockFile` in the per-user data directory), `--version`, and `--self-test` (builds the window
offscreen, checks the entry counts, calls the tray-availability query, exits 0). The CMake option
`DOFSCROLL_UI` defaults to ON only when Qt6 Widgets is found. Workflow:
`.github/workflows/ui.yml`, not a required check.
