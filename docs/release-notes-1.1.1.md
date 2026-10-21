<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.1.1

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Fix release for [1.1.0](release-notes-1.1.0.md). Nothing in the daemon, the interface or the configuration
changes.

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

## Not done

Windows restarts the daemon and the interface after an update already. On macOS the DMG is installed by dragging the
app, nothing runs, so the old daemon keeps running until the next login (#146).

## Tested

The upgrade script is tested with fake `loginctl`, `systemctl`, `pgrep` and `kill` programs
(`tests/test_postinst_migration.sh`); a real `dnf` upgrade from 1.1.1 has not been run.
