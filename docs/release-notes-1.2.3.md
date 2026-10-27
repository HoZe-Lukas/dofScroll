<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.3

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. **Windows: the basic functions work on a real machine
> (first tests, SpaceMouse Pro, official 3Dconnexion driver)**; the macOS build is built and unit-tested by CI but has
> never been run with a real device.

Release after [1.2.2](release-notes-1.2.2.md), mainly the findings of the first tests on Windows, plus two
fixes for Ubuntu that came from a contributor. The configuration format does not change.

**Highlights**

- Windows: remove the 3Dconnexion driver's own axis settings, so programs no longer get every movement twice.
- A first-start default for each operating system. Windows scrolls by pushing and tilting.
- The settings window is no longer empty under a regional format with a decimal comma (`de_DE`, `it_IT`, ...).
- The daemon starts as a service on Ubuntu 24.10 and newer (AppArmor).

## Windows: cleanup of the 3Dconnexion driver's axis settings

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

## First start: a default per operating system

The first start seeds the example configuration of the running system (`examples/default/linux`, `windows`,
`macos`), because the axis order differs. Windows scrolls by pushing and tilting
(`SCROLL_V_MIX = y + rx`, `SCROLL_H_MIX = x - ry`, `ZOOM_MIX = -rz`). The macOS default is experimental and untested
(Linux values). Existing configurations are not touched.

The seeded file no longer contains a device profile that switched off sideways scroll for one device. On a new
installation the interface adds `[device:<name>]` (button 1 = navigation) for the first device the daemon
reports, once.

## Fixes for Ubuntu and other Linux systems

- **The settings window was empty with a decimal-comma locale.** The style sheet lengths were written as
  `0,571em`, which Qt cannot parse, so the window showed only its title bar. They are now written and read
  independent of the locale. For an older release, start `LC_NUMERIC=C.UTF-8 dofscroll-ui`
  ([troubleshooting.md](troubleshooting.md)).
- **The user units failed with `status=218/CAPABILITIES`** on distributions that confine unprivileged user
  namespaces with AppArmor (Ubuntu 24.10 and newer), so the daemon never started as a service. The reason and the
  manual repair for an installed older release are in [troubleshooting.md](troubleshooting.md).

## Other fixes

- Zoom never engaged when the twist axis was also listed in `ZOOM_LATERAL`. The daemon now ignores axes of the
  zoom mix there; the axis assistant writes `ZOOM_LATERAL` from the scroll axes.
- Windows: `dofscroll.exe`, `dofscroll-ui.exe` and the installer now have the dofscroll icon (taskbar).

## Backward-incompatible changes

None for the configuration. One change in behavior for the Linux user units: they no longer set
`CapabilityBoundingSet=`, `ProtectKernelModules=`, `ProtectKernelLogs=` and `ProtectClock=`, so the sandbox is a
little less strict. The rest of the hardening stays; how to set the four directives again on a system that allows
them is in [security.md](security.md).

## All changes

- Windows: cleanup dialog for the 3Dconnexion driver's axis settings (#185); several programs can be ticked (#187)
- Per-OS first-start defaults, first device profile, zoom fix, Windows icon (#183); Windows default scrolls by
  pushing and tilting (#187)
- User units: drop the four directives that abort the start with 218/CAPABILITIES on AppArmor systems (#194) by
  @codingkoala, reported in #191
- Interface: write and read em values without the locale (#195) by @codingkoala, reported in #192
- New workflow `tag-release` creates the release tag from the Actions tab (#182, [releasing.md](releasing.md))
- Website (English and German) and wiki: the driver cleanup, Windows status after the first tests on a real
  machine (#188)
- Build inputs: refreshed CI pins, brotli 1.2.1 (#198); CodeQL action 4.39.1 (#196)

## Thank you

Thanks to @codingkoala for the reports with logs and reproductions on Ubuntu 26.04 (#191, #192, #193) and for the
two fixes (#194, #195).

## Not verified

The cleanup dialog (ticking several rows with mouse and keyboard, backup and restore against a real driver
installation) and scrolling by pushing and tilting in both directions with the new Windows default need a human
on Windows. The macOS default and image have not been run on a real Mac. Apart from the two Linux fixes, Linux
behavior is unchanged but was not re-tested on hardware for this release. The two fixes were verified on Ubuntu
26.04 by their author; the start of the units in a fresh graphical session and on RHEL 9 without the four
directives still need a human.

## Known limitations

- The interface assumes the Linux axis names (`y` = lift/press, `ry` = twist). With the official driver on Windows
  the axes are named differently, so the lift/press warning on the Channels page and the labels of the motion
  editor and motion help can be wrong there. The scrolling itself is not affected. Tracked in issue #186.
- `dofscroll-ui --self-test` still fails on a real Wayland session at the info popover check (the grabbing popup
  is refused); the normal use of the interface is not affected. Tracked in issue #193.
