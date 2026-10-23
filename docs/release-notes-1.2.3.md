<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.3

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. **Windows: first tests on a real machine
> (SpaceMouse Pro, official 3Dconnexion driver)**; the macOS build is built and unit-tested by CI but has
> never been run with a real device.

Release after [1.2.2](release-notes-1.2.2.md), mainly the findings of the first tests on Windows. The
configuration format does not change.

## What changed

### Windows: cleanup of the 3Dconnexion driver's axis settings (#185, #187)

The official driver keeps one XML file per program. An axis that is switched on there (for example the twist
to the mouse wheel) makes that program receive every movement twice: from the driver and from dofscroll.
New on the General page (Windows): box **3Dconnexion driver** with **Remove driver axis settings...**

- It lists the programs with active driver axes and what each axis does. Everything is ticked except programs
  whose axes only pass the motion on (3D navigation of CAD programs). Several rows can be ticked (mouse or
  keyboard), with **Tick all** and **Tick none**.
- **Switch off the ticked programs** sets the axes of those files to disabled. Before a file is changed the
  original is copied to `Cfg\dofscroll-backup\<date>-<time>\`; the result is read back and the original is put
  back if it did not hold. **Restore the last backup** puts the newest backup back.
- `Global.xml`, `UserInfo.xml` and the driver's installed defaults are not touched.

See [ui.md](ui.md) and [troubleshooting.md](troubleshooting.md).

### First start: a default per operating system (#183, #187)

The first start seeds the example configuration of the running system (`examples/default/linux`, `windows`,
`macos`), because the axis order differs. Windows scrolls by pushing and tilting
(`SCROLL_V_MIX = y + rx`, `SCROLL_H_MIX = x - ry`, `ZOOM_MIX = -rz`). The macOS default is experimental and untested
(Linux values). Existing configurations are not touched.

The seeded file no longer contains a device profile that switched off sideways scroll for one device. On a new
installation the interface adds `[device:<name>]` (button 1 = navigation) for the first device the daemon
reports, once.

### Fixes

- Zoom never engaged when the twist axis was also listed in `ZOOM_LATERAL`. The daemon now ignores axes of the
  zoom mix there; the axis assistant writes `ZOOM_LATERAL` from the scroll axes.
- Windows: `dofscroll.exe`, `dofscroll-ui.exe` and the installer now have the dofscroll icon (taskbar).

### Website, wiki and workflow

The driver cleanup is highlighted on the website (English and German) and in the wiki. New workflow
`tag-release` creates the release tag from the Actions tab ([releasing.md](releasing.md)).

## Not verified

The cleanup dialog (ticking several rows with mouse and keyboard, backup and restore against a real driver
installation) and scrolling by pushing and tilting in both directions with the new Windows default need a human
on Windows. The macOS default and image have not been run on a real Mac. Linux behavior is unchanged but was
not re-tested on hardware for this release.
