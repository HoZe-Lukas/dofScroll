<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.1

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Fix release after [1.2.0](release-notes-1.2.0.md). Everything of 1.2.0 applies; the program itself is unchanged.

## What changed

### macOS: the interface is back in the disk image

The macOS disk image of 1.2.0 (`dofscroll-1.2.0-macos-arm64-daemon-only.dmg`) contained only the daemon, without the
tray and settings window. The reason was in the build, not in the program: the hash-pinned installer of Qt
(`packaging/ci/aqt-requirements.txt`) listed `backports.zstd`, which exists only for Python before 3.14, and the macOS
build runner has Python 3.14. The Qt step is allowed to fail (so that the daemon can still be shipped), which is
why nothing turned red. The requirement now carries the marker `python_version < "3.14"`, and
`scripts/update-aqt-requirements.sh` writes such markers itself. The Linux and Windows packages of 1.2.0 were complete.

If you installed the 1.2.0 macOS image, install this one instead.

## Not verified

The macOS image is built and checked by CI (mount, signature, start); it has not been run on a real Mac.
