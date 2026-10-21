<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.1.1

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Fix release for [1.1.0](release-notes-1.1.0.md). The configuration format does not change.

## What changed

- **A package upgrade restarts the daemon and the interface (#144).** After `dnf install` of 1.1.0 over an older
  version, `dofscroll` and `dofscroll-ui` kept running the replaced program for two kinds of users: those whose
  user name contains an `@` (domain accounts such as `user@domain`), which the upgrade step skipped, and those whose
  interface had been started by the desktop's session restore instead of `dofscroll-ui.service`, which the restart of
  the unit does not reach. Both are handled now: the upgrade restarts the units for such names, and an interface
  that runs a replaced program is stopped and started again through its unit (only if the unit is enabled). The fix
  lives in the package's upgrade script, so it takes effect when you upgrade **from** 1.1.1 to a later version. When
  you upgrade from 1.1.0 to 1.1.1, restart once by hand: `systemctl --user daemon-reload`, `systemctl --user restart
  dofscroll.service`, then stop a running `dofscroll-ui` and `systemctl --user start dofscroll-ui.service`, or log out
  and in again ([installation.md](installation.md#3-upgrade-uninstall-migration)).

- **Change the program or device of a profile (#147).** "Rename..." (a bare text box) is now **Change application...** for
  program profiles and **Change device...** for device profiles, with the same picker as adding a profile (running programs,
  capture the next focused program, device list, manual entry); the settings stay, which helps when a program's name
  changed. **Change, Export and Remove** moved from below the profile list into the box "This profile" at the bottom of the
  General tab (Global has only Export). A program or device that already has a profile cannot be chosen.
- **Fixes from the review before this release.**
  - A package upgrade stops an interface that runs a replaced program only for a user who has the unit enabled
    (before, an interface that was not enabled could be stopped and not come back).
  - When one device is replaced by another model in a single change, the daemon keeps one lane for the new device
    (before, an empty lane could stay next to it, and the status file described the empty one).
  - The profile page's buttons run after the click finished (they rebuilt the tab they sit in).
- **Security fixes** ([security.md](security.md), section 14): importing a joystick mode or button mode file imports
  exactly the content that was checked and shown, not the file read again; names from files, programs and devices are
  shown as plain text, not rich text; the upgrade script (runs as root) refuses user names that start with `-`, is
  time-limited in every call and re-checks the owner of a process before signalling it; on Windows the installer and the
  interface start PowerShell by its full path, the uninstaller removes the folder only when `dofscroll.exe` is in it, and
  the folder picker refuses a folder that is not empty and not a dofscroll install.

## Not done

Windows restarts the daemon and the interface after an update already. On macOS the DMG is installed by dragging the
app, nothing runs, so the old daemon keeps running until the next login (#146).

Open and tracked: pinned CI images and a hashed Qt download for release builds (#149), fixed directories for the
interface's helper programs (#150).

## Tested

The upgrade script is tested with fake `loginctl`, `systemctl`, `pgrep` and `kill` programs
(`tests/test_postinst_migration.sh`); a real `dnf` upgrade from 1.1.1 has not been run.
The new profile page code, the dialogs and the installer changes were compiled and self-tested by CI only; the look
of the "This profile" box and the Windows installer's folder check need a human on a real desktop and a real Windows.
