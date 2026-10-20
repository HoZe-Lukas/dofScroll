<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.0.1

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers (Rocky Linux 9, Debian 12 and 13, Ubuntu 24.04,
> Fedora 42). The **Windows and macOS builds are built and unit-tested by CI but have never been run with a
> real device.**

Fix release for [1.0.0](release-notes-1.0.0.md). The daemon, the configuration and the command line do not
change; the way the Qt interface gets its Qt does.

## What changed

- **The Linux packages bring their own Qt (#116).** The 1.0.0 RPM was built against Qt 6.10.3 from EPEL, so it
  required `libQt6Core.so.6(Qt_6.10)`: a RHEL 9 system with an older Qt 6 (for example Qt 6.6.2 from an EPEL
  mirror) could only install it by upgrading the whole Qt 6 stack, or not at all. Now the RPM, the DEB and the
  TGZ contain **Qt 6.9.3** and every library it needs in `/usr/lib/dofscroll` (Qt, ICU, the X11 and Wayland
  platform plugins, libX11 and the xcb libraries, xkbcommon, fontconfig, freetype, harfbuzz, glib, dbus, ...). They
  need no Qt from the system, none from EPEL, and do not use one that is installed: they install on RHEL 9 with
  any Qt 6 or none, and the same on Debian 12 and Ubuntu 24.04. The package dependencies are `spacenavd` (the
  device service, from EPEL on the RHEL family) and the daemon's libraries; glibc and `libstdc++` stay on the system.
  Packages are about 27 MB (RPM), 34 MB (DEB) and 35 MB (TGZ).
- **The TGZ is self-contained**: unpack it anywhere and run `usr/bin/dofscroll-ui`; it needs only the C library
  (checked in bare Rocky 9, Debian 12, Ubuntu 24.04 and Fedora 42 containers).
- **One Qt version everywhere.** Linux, Windows and macOS use Qt 6.9.3 (Windows moved from 6.8.3), set in one
  file (`packaging/qt.env`) that all CI jobs read; a weekly check warns about new Qt patch releases, because a
  Qt security update is now delivered by a dofscroll release ([release-process.md](release-process.md)).
- `dofscroll-ui --version` prints the Qt it runs on on a second line (the first line is unchanged).
- Licenses: the packages ship the license texts of Qt (LGPL-3.0, dynamically linked and replaceable) and of every
  bundled library, an inventory `bundled-libraries.txt` and `THIRD-PARTY-NOTICES.md` in
  `/usr/share/doc/dofscroll/` (and next to the program on Windows and macOS).
- CI: the package job fails if the RPM requires Qt or a bundled library or provides one; the install smoke test
  runs the interface self-test on five distributions and the TGZ on four, and checks that Qt is loaded from the
  package.
- The DEB depends no longer on `libqt6*` packages of the distribution.

## Upgrading

`sudo dnf install ./dofscroll-1.0.1-1.x86_64.rpm` (EPEL enabled for `spacenavd`, see
[installation.md](installation.md)); `sudo apt install ./dofscroll_1.0.1_amd64.deb`. An older Qt that the
1.0.0 RPM pulled in stays installed and is simply no longer used by dofscroll.

## Tested on and known limitations

As for 1.0.0. The new packages were built and install-tested in CI containers (build, install, `dofscroll-ui
--self-test` offscreen, X11 and Wayland plugins load, TGZ in bare images) and the interface was run against a
virtual X server. Not verified: the tray icon, the look under KDE Plasma and GNOME with the bundled Qt (Qt's own
Fusion style with the desktop's light or dark scheme, no KDE or GNOME style plugin), native Wayland sessions, and
installation on a real RHEL 9 machine; only the maintainer can do that.
