<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release process and installers

How a release is made is in [releasing.md](releasing.md) (tag `v<version>`, the release job
publishes). This page describes what the tag builds for each OS and how signing would be added.
Decision for version 0.1: **all Windows and macOS packages are unsigned**.

## Artifacts per OS

| OS | File | Built by |
|---|---|---|
| Linux | `dofscroll-<v>-Linux.rpm`, `.deb`, `.tar.gz` | jobs `package-rpm`, `package-deb` in `linux-packages.yml` (called by `ci.yml`; dry run: `linux-packages-dry-run.yml`) |
| Windows | `dofscroll-<v>-windows-x64-setup.exe` (per-user installer) and `dofscroll-<v>-windows-x64-portable.zip` | job `package-windows` in `installers.yml` |
| macOS | `dofscroll-<v>-macos-arm64.dmg` | job `package-macos` in `installers.yml` |
| all | `SHA256SUMS.txt` over every file above | job `release` in `ci.yml` |
| all | build provenance attestation for every file above (`gh attestation verify`, see [security.md](security.md) section 14) | job `release` in `ci.yml` (`actions/attest-build-provenance`) |

Install smoke test: after packaging, `linux-smoke.yml` (called by `ci.yml` as job `linux-smoke` and
by `linux-packages-dry-run.yml`) downloads `packages-rpm-tgz` and `packages-deb` and installs them
in fresh containers (RPM: Rocky 9, Fedora 42; DEB: Debian 12, Debian 13, Ubuntu 24.04; the TGZ unpacked in
bare Rocky 9, Debian 12, Ubuntu 24.04 and Fedora 42 containers), then runs
`dofscroll --version`, `dofscroll --help`, `dofscroll-ui --version` and the offscreen `dofscroll-ui --self-test` via
`scripts/smoke-install.sh`, with the Qt checks described under "Qt in the packages". Only Rocky 9 and
Debian 12 are required entries; the others are
`continue-on-error`. The `release` job does not list `linux-smoke` in `needs`, so the smoke test never
gates publishing: read its summary before announcing a release. Per-distro verdicts are in the job
summary.

`ci.yml` calls `installers.yml` (job `installers`, tags only); the `release` job needs it, so a
tag only produces a release when every package was built. The same reusable workflow runs from
`installers-dry-run.yml` (manual `workflow_dispatch`, and on pull requests that touch installer
files): it uploads the artifacts `installers-windows` and `installers-macos` and creates no release.
Test a change to the installers with `gh workflow run installers-dry-run.yml --ref <branch>` (the
workflow file must exist on the default branch for `gh workflow run`; before that the pull request
run does the same).

There is no daemon-only fallback: every Windows and macOS package contains the interface and the daemon. If Qt
cannot be installed the job fails, `build-dmg.sh` and `build-installer.ps1` refuse to build without
`dofscroll-ui`, the image and installer tests require it, and the `release` job publishes nothing unless all six
files (RPM, DEB, TGZ, Windows setup and ZIP, macOS DMG) exist and none has `daemon-only` in its name. (Version 1.2.0
shipped a macOS image without the interface because that step used to be allowed to fail.) The Qt version is the one of `packaging/qt.env` (the same on
Windows, macOS and Linux; Windows kit `win64_msvc2022_64`), installed with a pinned `aqtinstall` instead of `install-qt-action` (that action uses an unpinned `setup-python`, which the repository action policy rejects); all actions are pinned to commit SHAs.

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
in a plain folder and in a folder with a space in its name (Windows restricts the characters
in paths, so the test stays basic); the macOS job uses a folder with spaces, umlauts, CJK, an emoji and an ampersand and runs the
daemon, the UI self-test and the LaunchAgent install/uninstall (in a sandbox `HOME`, nothing is
loaded into launchd) from such a folder.

## Release-path downloads

No release step depends on a package feed being up at that moment:

- **NSIS** comes from `scripts/install-nsis.ps1`: the official zip (pinned version), a pinned SHA-256
  that fails the step on a wrong file, 3 attempts with a pause per source, an optional mirror
  (repository variable `NSIS_MIRROR_URL`, same file) tried first, and `actions/cache` for the zip.
  `installers-dry-run.yml` calls the same reusable workflow, so a broken source shows up before a
  tag. To bump NSIS, change version and SHA-256 in the script and the cache key in `installers.yml`.
- **aqtinstall and Qt** (pip, `aqt`) are retried 3 times; a failure fails the job (no daemon-only
  fallback).
- Audit, not yet hardened: the Linux package jobs (`linux-packages.yml`: `dnf`/`apt` inside
  containers) and `ci.yml` (`dnf`) rely on distribution mirrors, which retry internally; the macOS
  job uses only aqtinstall (retried) and the runner's tools.

## CI build images

The Rocky Linux 9 jobs (`build + ctest`, ASan/UBSan, cppcheck, CodeQL and the RPM package) run in
prebuilt images from `ghcr.io/hoze-lukas/dofscroll-ci-rocky9` (and `-qt`, for the RPM job, which adds
the Qt of `packaging/qt.env`, see "Qt in the packages") instead of running `dnf` on every run. `packaging/ci/rocky9.Dockerfile` is the recipe;
`.github/workflows/ci-image.yml` builds and checks the images on pull requests that touch it, and
pushes them on `main` and every Monday (distribution updates). The packages are public, so jobs pull
them without a login.

A pull request that needs a new build dependency changes the Dockerfile; its own jobs still use the
old image, so add the dependency in one pull request, merge it (the push to `main` publishes the new
image) and use it in the next one. If the images are ever missing or broken, the Rocky jobs fail at
the start: run the `ci-image` workflow by hand (Actions tab, "Run workflow").

## Qt in the packages

Every package that ships the interface ships **the same Qt**: the Linux packages (RPM, DEB, TGZ), the
Windows installer and the macOS disk image bundle Qt 6.9.3 instead of using the Qt of the system. The
version, and the aqtinstall version that downloads it, are in ONE file, `packaging/qt.env`; the CI
image (`packaging/ci/rocky9.Dockerfile`, `packaging/ci/debian12-qt.sh`), `ui.yml`, `installers.yml`,
`linux-packages.yml`, `linux-smoke.yml` and `qt-update-check.yml` read it.

When you change `AQT_VERSION`, also run `scripts/update-aqt-requirements.sh` and commit
`packaging/ci/aqt-requirements.txt`: aqtinstall and its dependencies are installed from that file with
`pip install --require-hashes`, and `scripts/check-ci-pins.sh` (a test) fails when the two disagree.
aqtinstall itself checks every Qt archive against the SHA-256 that download.qt.io publishes next to it.

Why bundle on Linux (#116): a program built against Qt 6.N requires `libQt6Core.so.6(Qt_6.N)`, so the 1.0.0
RPM, built against the newest EPEL Qt (6.10.3), could not be installed on RHEL 9 systems with the older
EPEL Qt 6.6. Building against the oldest Qt would have helped only for the RPM; bundling makes all packages
independent of the Qt of the system, the same everywhere, and tested with the Qt that is shipped.

How it works (Linux):

- The CI image and the Debian 12 job install Qt from download.qt.io with aqtinstall (`/opt/qt/current`) and the
  runtime libraries that the Qt binaries link, so `scripts/bundle-qt-linux.sh` can copy them.
- The release packages are configured with `-DDOFSCROLL_BUNDLE_QT=ON` (`cmake/Ui.cmake`). `cmake --install`
  (and so CPack) runs `scripts/bundle-qt-linux.sh`, which follows the dependencies of `dofscroll-ui` and of the
  chosen Qt plugins with `ldd`, copies everything except glibc, `libstdc++` and `libgcc_s` into
  `usr/lib/dofscroll/lib` and `usr/lib/dofscroll/plugins`, strips the files, sets the RPATH of the libraries
  to `$ORIGIN` (plugins: `$ORIGIN/../../lib`) with patchelf, verifies that everything resolves inside the
  bundle, and writes `bundled-libraries.txt` (source package, version, license) and the license files of the
  distribution packages into `usr/share/doc/dofscroll/third-party/`. `dofscroll-ui` gets the RPATH
  `$ORIGIN/../lib/dofscroll/lib`, so the TGZ works from any folder. The script fails when a library, a license file or the
  ICU license text of the ICU version that Qt ships is missing. The list of plugins (xcb, Wayland, offscreen,
  portal theme, SVG and image formats) is in the script.
- RPM: CPack passes spec macros (`DofscrollCPack.cmake`): the private libraries are no `Provides`, the Qt and OpenGL
  sonames of the executable and the files below `lib/dofscroll` are no `Requires`, and no build-id links are
  made (the bundled copies would collide with the system libraries). DEB: `dpkg-shlibdeps` looks into the
  private directory. The packages depend only on `spacenavd` and the daemon's libraries.
- The qt.io Qt needs glibc 2.28 or newer; the RPM and TGZ are built on Rocky Linux 9 and the DEB on Debian 12,
  the oldest supported distributions, so the copied libraries run on newer ones.

Checks:

- `linux-packages.yml` (tag build and dry run): `rpm -qpR` and `--provides` must contain no Qt, OpenGL, xcb or other
  bundled library, the layout and license files must be there, the RPATH must be the relative one, and
  `dofscroll-ui --version` must report the Qt version of `packaging/qt.env`; the job summary lists the requirements
  and the sizes. The DEB job checks `Depends` the same way.
- `linux-smoke.yml` (install smoke test, `scripts/smoke-install.sh`): RPM on Rocky Linux 9 and Fedora 42, DEB on
  Debian 12 and 13 and Ubuntu 24.04, TGZ unpacked in bare Rocky 9, Debian 12, Ubuntu 24.04 and Fedora 42 containers. The test asserts
  that no Qt of the distribution was installed, `ldd /usr/bin/dofscroll-ui` resolves Qt, ICU and OpenGL from `lib/dofscroll`,
  `--version` prints the expected Qt, `--self-test` passes offscreen, and the xcb and Wayland plugins load (they start and
  report that there is no display).
- `ui.yml`: `ui-linux` builds with the Qt of Ubuntu (6.4, the oldest Qt the sources support; building from source with
  a system Qt must keep working), `ui-linux-pinned`, `ui-windows` and `ui-macos` with the Qt of `packaging/qt.env`.
- `qt-update-check.yml` runs weekly and warns when a newer patch release of the pinned minor version exists.
- Not verifiable in CI (needs a real desktop): the tray icon, the look under KDE Plasma and GNOME, and a native
  Wayland session. The bundled Qt has no KDE or GNOME style plugin: the interface uses Qt's Fusion style with the
  colours of the desktop portal.

To bump Qt: change `QT_VERSION` in `packaging/qt.env` (one line). Then check
`bundle-qt` messages of the build: when Qt ships another ICU version, add
`third-party/licenses/ICU-<version>-LICENSE.txt` and update `THIRD-PARTY-NOTICES.md`, and run the
`ci-image` workflow (it rebuilds the `-qt` image; the package job fails with a clear message while the image still
has the old Qt). Merge the `packaging/ci` change first, because the image is published from `main`. Windows and
macOS use the Qt version from the same file; the macOS build needs Qt 6.9 or newer (6.8 links the AGL
framework, dropped in the macOS 15 SDK).

**Security updates of Qt are our job now.** The packages bundle Qt and its libraries, so a Qt security fix reaches
users only with a new dofscroll release (until now the distribution's Qt was updated by the distribution). Qt 6.9
is a non-LTS branch: its patch releases stop some months after the next minor version appears, so plan a move to
the then current LTS branch (6.8 or a later LTS) or the next minor version when the weekly check shows that 6.9
gets no more patch releases. The weekly `qt-update-check` workflow warns about new patch releases; the
bundled system libraries (glib, freetype, libpng, ...) come from Rocky Linux 9 or Debian 12 at build time and are updated
by rebuilding in the refreshed images (the CI image is rebuilt weekly).

## Windows installer

- **Tool: NSIS 3.** It is free, needs no .NET or WiX toolset, is installed in CI by
  `scripts/install-nsis.ps1` (pinned zip, see below), compiles a single `.nsi` text file, and supports per-user
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
  version from `CMakeLists.txt`, an icon rendered from `icons/dofscroll-app.svg` (skipped with a
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
