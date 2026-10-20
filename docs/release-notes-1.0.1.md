<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.0.1

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers (Rocky Linux 9, Debian 12 and 13, Ubuntu 24.04,
> Fedora 42). The **Windows and macOS builds are built and unit-tested by CI but have never been run with a
> real device.**

Fix release for [1.0.0](release-notes-1.0.0.md). Nothing changes in the programs, the configuration or the
command line; only the Linux RPM is built differently.

## What changed

- **The RPM needs Qt 6.6 instead of Qt 6.10 (#116).** The 1.0.0 RPM was built against Qt 6.10.3, so it
  required `libQt6Core.so.6(Qt_6.10)`. A RHEL 9 system with an older Qt 6 (for example Qt 6.6.2 from an
  EPEL mirror) could only install it by upgrading the whole Qt 6 stack to 6.10, or not at all. The RPM is
  now built against Qt 6.6.3, the oldest supported Qt, and requires `Qt_6.6` at most: it installs on RHEL 9
  and its clones with Qt 6.6 or newer, without upgrading Qt, and it also runs with the newest Qt (checked
  in CI against Qt 6.10 from EPEL). The dependencies are `qt6-qtbase-gui >= 6.6` and `qt6-qtsvg >= 6.6`.
- **CI keeps it that way.** The package job fails if the RPM requires a Qt symbol version above 6.6, a new
  interface build against Qt 6.6.3 runs for every pull request (code that needs a newer Qt does not
  compile), and the install smoke test prints the Qt version it ran with. How the Qt version is chosen and
  changed: [release-process.md](release-process.md).
- The DEB is unchanged: it is built on Debian 12 with Debian's own Qt 6.4.

## Upgrading

`sudo dnf install ./dofscroll-1.0.1-1.x86_64.rpm` (EPEL enabled, see [installation.md](installation.md)).
Windows and macOS packages are the same programs as 1.0.0 with the new version number.

## Tested on and known limitations

As for 1.0.0. The new RPM was checked in CI containers (build, install on Rocky Linux 9 with the Qt of
EPEL, `dofscroll-ui --self-test`). Not verified: installation on a real RHEL 9 machine with Qt 6.6, which
only the maintainer can do.
