<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.2

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Release after [1.2.1](release-notes-1.2.1.md). The command line and the configuration format do not change; the
daemon only refuses to run twice (see "First start" below).

## What changed

### Joystick modes list: two lines of three (#178)

In 1.2.0 and 1.2.1 the compact chips in the list of joystick modes showed only the new marker (bar, wedge, arrow) and
hid the symbol of the action. Now every chip is the marker and the action symbol, and the six chips sit in two lines of
three like the panel of the detail view: the pushes (vertical, horizontal, lift / press) above, the tilts and the
twist below. The legend under the list says so. The symbols are no longer cut off ("..."), also with other fonts.

### Linux: the package stops the programs before installing and starts them again afterwards

An upgrade no longer needs a restart by hand. The RPM (`%pre`/`%post`) and the DEB (`preinst`/`postinst`) now stop
`dofscroll-ui.service` and `dofscroll.service` of every logged-in user **before** the files are replaced, and start
both again **afterwards** (the daemon first), whether they were running before or not. Before, the package only did a
`try-restart` after the files were in place, which did not always replace a running interface or daemon. A fresh
install does nothing. The steps are best effort and never fail the upgrade. See [installation.md](installation.md#3-upgrade-uninstall-migration).

### First start: just open the program, on Linux, Windows and macOS

After installing, the first start is opening dofScroll from the start menu, the application launcher or the
Applications folder. No `systemctl --user enable --now ...`, no script, no terminal:

- **The interface starts the daemon** when none runs (judged from the status heartbeat, after a grace period of about
  6 seconds so a login autostart does not produce two daemons): Linux `systemctl --user start dofscroll.service`
  (the binary if that fails), macOS `launchctl kickstart` or the daemon inside the app, Windows `dofscroll.exe`.
- **The daemon refuses to run twice** on Linux and macOS (an exclusive lock in its private runtime directory, released
  by the kernel when the process ends; Windows already had a named mutex). A second daemon exits at once with a log line.
- **A setup guide** shows with the window on the first start of a new installation: dofscroll is running, SpaceMouse
  found (on Linux, the `spacenavd` fix as a command to copy if the service is off), Start at login (on by default),
  on macOS the Input Monitoring and Accessibility permissions with a button that opens System Settings, and "Try it".
  It can be skipped, finished and opened again from the Overview ("Show setup guide"). See [ui.md](ui.md#the-setup-guide).
- **The Linux packages enable `spacenavd.service`** on every install and upgrade (best effort, as root, only when the
  unit exists and is not enabled). A fresh install does not start your dofscroll units.
- macOS: the app image now finds its daemon in `Contents/Resources/daemon` for "Start at login", Restart and Release
  all keys (they looked for it next to the interface). The Windows installer's finish page offers "Open dofscroll now".

Only a human on real hardware can confirm the first start from a real start menu, the macOS permissions flow and the
daemon start through systemd.

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
