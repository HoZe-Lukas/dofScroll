<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.2

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Release after [1.2.1](release-notes-1.2.1.md). The daemon, the command line and the configuration format do
not change.

## What changed

### Joystick modes list: two lines of three (#178)

In 1.2.0 and 1.2.1 the compact chips in the list of joystick modes showed only the new marker (bar, wedge, arrow) and
hid the symbol of the action. Now every chip is the marker and the action symbol, and the six chips sit in two lines of
three like the panel of the detail view: the pushes (vertical, horizontal, lift / press) above, the tilts and the
twist below. The legend under the list says so. The symbols are no longer cut off ("..."), also with other fonts.

### Linux: the package stops the programs before installing and starts them again afterwards

An upgrade no longer needs a restart by hand. The RPM (`%pre`/`%post`) and the DEB (`preinst`/`postinst`) now stop
`dofscroll-ui.service` and `dofscroll.service` of every logged-in user whose units are running **before** the files
are replaced, and start exactly those units again **afterwards** (the daemon first). Before, the package only did a
`try-restart` after the files were in place, which did not always replace a running interface or daemon. A user whose
units were not running gets nothing started; a fresh install does nothing. The steps are best effort and never fail
the upgrade. See [installation.md](installation.md#3-upgrade-uninstall-migration).

### Windows and macOS: never without the interface (#178)

Every Windows installer, Windows ZIP and macOS image contains the settings window and the daemon. If Qt cannot be
installed in a build job, the job fails (before, the macOS image of 1.2.0 was built without the interface and nothing
turned red). The packaging scripts refuse to build without `dofscroll-ui`, the image tests require the app, and the
release job publishes nothing unless the RPM, DEB, TGZ, Windows setup, Windows ZIP and macOS DMG all exist and none is
named `daemon-only`.

### Website and readme

New screenshots of the interface (English and German), made with Qt 6.9.3.

## Not verified

The look of the list (symbols in the real fonts of Windows and macOS, wrapping, sizes) needs a human on each system.
The macOS image is built and checked by CI (mount, signature, start); it has not been run on a real Mac.
