<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release process and installers

How a release is made is in [releasing.md](releasing.md) (tag `v<version>`, the release job
publishes). This page describes what the tag builds for each OS and how signing would be added.
Decision for version 0.1: **all Windows and macOS packages are unsigned**.

## Artifacts per OS

| OS | File | Built by |
|---|---|---|
| Linux | `dofscroll-<v>-Linux.rpm`, `.deb`, `.tar.gz` | jobs `package-rpm`, `package-deb` in `ci.yml` |
| Windows | `dofscroll-<v>-windows-x64-setup.exe` (per-user installer) and `dofscroll-<v>-windows-x64-portable.zip` | job `package-windows` in `installers.yml` |
| macOS | `dofscroll-<v>-macos-arm64.dmg` | job `package-macos` in `installers.yml` |
| all | `SHA256SUMS.txt` over every file above | job `release` in `ci.yml` |

`ci.yml` calls `installers.yml` (job `installers`, tags only); the `release` job needs it, so a
tag only produces a release when every package was built. The same reusable workflow runs from
`installers-dry-run.yml` (manual `workflow_dispatch`, and on pull requests that touch installer
files): it uploads the artifacts `installers-windows` and `installers-macos` and creates no release.
Test a change to the installers with `gh workflow run installers-dry-run.yml --ref <branch>` (the
workflow file must exist on the default branch for `gh workflow run`; before that the pull request
run does the same).

If Qt cannot be installed in a job (the `install-qt-action` step is `continue-on-error`), that job
builds the daemon without the UI and warns; the file names then contain `-daemon-only`
(`dofscroll-<v>-windows-x64-daemon-only-setup.exe`, `...-daemon-only-portable.zip`,
`dofscroll-<v>-macos-arm64-daemon-only.dmg`). Qt versions are the ones of `ui.yml`
(6.8.3 MSVC on Windows, 6.9.3 on macOS), installed with a pinned `aqtinstall` instead of `install-qt-action` (that action uses an unpinned `setup-python`, which the repository action policy rejects); all actions are pinned to commit SHAs.

Both jobs test their package before uploading: the Windows job installs silently with autostart
selected, runs `dofscroll.exe --version` and `dofscroll-ui.exe --self-test` from the install folder
(without the runner's Qt on `PATH`), uninstalls and checks that the folder, the uninstall entry and
the autostart value are gone; the macOS job mounts the DMG, verifies the ad-hoc signature, runs the
daemon and the UI self-test from the bundle. Both unpack the package and run
`scripts/scan-package-paths.sh`, which fails when any file (binaries included) contains the runner
workspace, a CPack staging or a runner home path.

## Text encoding and special characters

Everything is UTF-8 / Unicode. The NSIS script uses `Unicode true`, all paths and the Run values
are quoted, and the PowerShell scripts are ASCII-only and use `-LiteralPath` for file paths. The
macOS LaunchAgent script XML-escapes the daemon path (`&`, `<`, `>`) and handles spaces and
non-ASCII characters. The dry-run checks this: the Windows job installs, upgrades and uninstalls
in a folder named with spaces, umlauts, CJK, an emoji and an ampersand; the macOS job runs the
daemon, the UI self-test and the LaunchAgent install/uninstall (in a sandbox `HOME`, nothing is
loaded into launchd) from such a folder.

## Windows installer

- **Tool: NSIS 3.** It is free, needs no .NET or WiX toolset, is installed on `windows-latest`
  (or by one `choco install nsis`), compiles a single `.nsi` text file, and supports per-user
  installs (`RequestExecutionLevel user`), silent mode (`/S`) and a components page for the
  optional autostart. WiX would need its toolset and MSI authoring for the same result; MSI per-user
  support is more involved. `packaging/windows/installer.nsi` is the script,
  `packaging/windows/build-installer.ps1` stages the files and calls `makensis`.
- Installs to `%LOCALAPPDATA%\Programs\dofscroll`: `dofscroll.exe`, `dofscroll-ui.exe`, the Qt
  runtime (`windeployqt`), the Microsoft C++ runtime DLLs of the build toolchain (app-local),
  `devices\`, `examples\`, `icons\`, `scripts\*.ps1`, `LICENSE`, `THIRD-PARTY-NOTICES.md`.
- Start menu folder with the program and an uninstall entry; entry in Settings > Apps (HKCU).
- Optional component "Start the daemon at logon" writes the Run value `dofscroll` (quoted path of
  `dofscroll.exe`), the same value that `scripts\install-autostart.ps1` writes. The uninstaller
  removes that value and the Startup shortcut of the script. User configuration in
  `%APPDATA%\dofscroll` is kept. Silent: `setup.exe /S /AUTOSTART=1 /D=<folder>`.
- The portable ZIP is the same folder without installer or registry entries.

## macOS image

- `dofscroll.app` (built by `packaging/macos/build-dmg.sh` around the CMake binaries):
  `Info.plist` with `LSUIElement` (menu bar program), bundle identifier `io.github.dofscroll.ui`,
  version from `CMakeLists.txt`, an icon rendered from `icons/dofscroll-tray.svg` (skipped with a
  note when Quick Look cannot render it), Qt frameworks added by `macdeployqt`.
- The daemon, `devices/`, `examples/` and the LaunchAgent scripts and plist template are in
  `Contents/Resources/` (`daemon/`, `launchagent/`), not in `Contents/MacOS`, so the bundle seals.
- The image also holds `README-first-run.txt` (Gatekeeper, Input Monitoring, Accessibility,
  LaunchAgent), `LICENSE`, `THIRD-PARTY-NOTICES.md` and an `Applications` link. A `.dmg` was chosen
  over a `.pkg` because nothing has to run as root and the app is dragged into place.
- **Ad-hoc signing** (`codesign --sign -`) is applied to the daemon and the bundle. It is not an
  identity: it only gives the arm64 binaries the signature the kernel requires to run them at all.
  Gatekeeper still treats the download as unidentified and unnotarized; the first-run steps are in
  [installation.md](installation.md). Only Apple silicon is built (`macos-latest`).

## Adding signing later (no secrets are in the repository)

Store everything as GitHub Actions secrets of the repository (or of an environment with required
reviewers) and pass them to the package jobs; never commit them and never print them.

- **Windows (Authenticode):** obtain a code signing certificate (an OV or EV certificate from a CA,
  or Azure Trusted Signing). Add secrets for the certificate (base64 PFX) and its password, or the
  Azure identity. After `makensis`, sign `dofscroll.exe`, `dofscroll-ui.exe` and the installer with
  `signtool sign /fd SHA256 /tr <timestamp URL> /td SHA256`, and sign the uninstaller by building
  with `!uninstfinalize`/`!finalize` in the NSIS script. A reputation-based SmartScreen warning
  disappears only for EV certificates or after the certificate has built reputation.
- **macOS (Developer ID and notarization):** needs a paid Apple Developer Program membership.
  Secrets: the Developer ID Application certificate (base64 `.p12`) and its password, a keychain
  password, and either an App Store Connect API key or an Apple ID with an app-specific password
  and team ID. Import the certificate into a temporary keychain, replace `codesign --sign -` by
  `codesign --force --options runtime --timestamp --sign "Developer ID Application: ..."` (inside
  out: daemon, frameworks, app), add an entitlements file if the hardened runtime needs one, then
  `xcrun notarytool submit <dmg> --wait` and `xcrun stapler staple <dmg>`. Sign the DMG itself too.
- Keep the dry-run workflow as the place where signing is tried first, with the secrets limited to an
  environment that only the tag workflow and the maintainer can use (pull requests from forks never
  receive secrets).
