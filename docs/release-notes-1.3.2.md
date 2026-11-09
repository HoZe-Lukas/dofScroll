<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.3.2

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Windows: tests on one real
> machine (SpaceMouse Pro, official 3Dconnexion driver); the driver restart of this release was run there by hand.
> macOS: built and unit-tested by CI, never run with a real device. See "Not verified".

Release after [1.3.1](release-notes-1.3.1.md). It is about the official 3Dconnexion driver on Windows and macOS and a
fix for the keyboard layout on X11; the configuration and the profile files do not change.

**Highlights**

- **The driver cleanup is a step-by-step assistant with real check boxes (Windows, #184, #352).** *Settings > General >
  3Dconnexion driver > Remove driver axis settings...* used to be one list in which only one program could be ticked.
  The assistant has six pages (Back, Next, Cancel, keyboard operable): introduction and confirmation, what was found
  (folder, counts, every skipped file with its reason), choose the programs, preview, apply with a progress bar, result.
  The programs are a table with a real check box per row; ticking does not depend on the row selection. **Tick all**,
  **Tick none**, **Tick all except 3D and CAD programs** (the default) and **Invert** act on the rows shown, a filter
  narrows them, and a counter says "n of m programs ticked". Every changed file is backed up first, and any backup
  (not only the last) can be restored.
- **Restart the 3Dconnexion driver from the interface (Windows and macOS).** The driver reads its settings files when
  it starts, its settings window has to be closed first, and it has no menu entry for a restart. *Restart the
  3Dconnexion driver...* (General page) and *Restart the driver now* (last page of the assistant) ask first, close the
  driver's settings window, end the driver service of the signed-in user and start it again, then say in words what
  happened. Only programs from the driver's own folder with the driver's own file names are touched, nothing of other
  users. The driver's profile server is left running on purpose: nothing starts it again once it was ended.
- **X11: the output follows keyboard layout changes (#369).** The keycode table was built once at start; when the
  desktop applied the layout later, keys such as `minus` and `z` were typed as `ssharp` and `y`. The table is now
  rebuilt when the layout changes.

**Documentation and checks**

- Every statement about the vendor driver, its interface, `spacenavd` and `libspnav` was checked against official and
  primary sources: 42 claims in [driver-docs-check.md](driver-docs-check.md), with the fixes (#360). That check also
  opened #354 (signs of the Windows and macOS default mixes) and #355 (buttons of Pro, Pro Wireless and Enterprise in
  report `0x1c`), see "Not verified".
- Audit after 1.3.1: stale wording after the release, a test that the documented keys are the keys the parser knows, and
  a version consistency check (`scripts/check-version.sh`) (#365).
- Repository and CI: tags are created by hand by the administrator, `tag-release.yml` is gone and `tags.json` equals
  the live ruleset (#370, issue #361); the secret scan finds fine-grained GitHub tokens (#367); shellcheck covers more
  scripts (#368); `check-pin-diff` lets only the digest of an image change (#366); the macOS image build compiles no
  test programs (#353); the action `install-qt-action` is at 4.5.0 (#351).

## Not verified

- **The assistant and the restart were not clicked through by a person on Windows before this text was written.**
  Checked: the rules and the order of the restart with a fake process list (`tests/test_ui_driver_restart.cpp`), the
  assistant's file operations on a copy of a real driver folder (13 programs; 7 switched off, 6 3D/CAD programs left,
  all restored), the offscreen self-test of the pages, and the restart of the driver service by hand on Windows 11 with
  3DxWare 10.9. **Not checked:** that the driver takes over the changed files after the restart in every case, that a
  running dofscroll daemon picks the driver up again afterwards, and the look of the assistant under the native style.
- **macOS:** the restart was never run on a Mac. The helper's name (`3DconnexionHelper`) is from the vendor's support
  pages, and whether macOS starts the helper again by itself after it ended is unknown (the code starts it only when
  nothing did). The cleanup itself still does not exist on macOS: the format of the driver's per-program preferences is
  not documented (survey in [windows-mac.md](windows-mac.md)).
- **#354:** on the direct HID path of one SpaceMouse Pro the measured signs are push away = y negative, tilt forward = rx
  negative, push right = x positive, tilt right = ry negative, twist clockwise = rz positive, so the Windows default mix
  (`y + rx`, `x - ry`) adds its terms. The signs on the driver's own path were not measured. Lift and press (z) were not
  separated cleanly.
- **#355:** buttons of Pro, Pro Wireless and Enterprise may arrive in a report the direct HID path does not read. The
  device used for the measurement has no buttons, so it stays open.
- Everything listed under "Not verified" in [1.3.1](release-notes-1.3.1.md) still holds.

## Backward-incompatible changes

None. The old driver cleanup dialog is replaced by the assistant; the backups it made (`Cfg\dofscroll-backup\...`) can be
restored by the new one.

## All changes

Credits: pull requests by the maintainer unless marked.

- Driver cleanup assistant (#352, issue #184); restart of the 3Dconnexion driver (Windows, macOS)
- X11: keyboard layout changes while running (#369)
- Docs: the vendor driver statements checked against sources (#360); audit after 1.3.1 (#365)
- Repository and CI: tags by hand (#370); secret scan (#367); shellcheck (#368); pin diff (#366); macOS image (#353);
  `install-qt-action` 4.5.0 (#351); CI pins (#348)
