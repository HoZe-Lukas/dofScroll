<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.0

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Release after [1.1.2](release-notes-1.1.2.md). Two new settings let you adapt the direction of the axes and the
zoom key to what you and your programs need, the tray has one Restart item, and the build and release pipeline is
harder to tamper with. The configuration format only gains keys: existing files keep working unchanged.

## What changed

### Direction of single axes: `INVERT_AXES` (#168)

`INVERT_AXES = z rx` flips the direction of the listed axes (`x y z rx ry rz`) before any mix, split part or zoom
detection sees them, in **every** joystick mode. Use it when, for example, pushing scrolls the wrong way but tilting
is right: push and tilt are separate axes, so each has its own check box on the Channels tab ("Invert direction
of"). Like all settings it can be set globally, per device and per program. The sign of a channel (`*_SIGN`) still
flips the whole channel.

### Zoom key per profile: `ZOOM_MODIFIER` (#168)

The keys held while a zoom action sends the mouse wheel are no longer fixed to Ctrl: `ZOOM_MODIFIER = ctrl` (the
default), `shift`, `alt`, `super` (the Command key on macOS), a combination such as `ctrl+shift`, or `none` (the
plain wheel). Set it per program or device where the zoom key differs, for example `super` on macOS. Channels tab:
"Zoom key".

### Clearer motion chips (#168)

- Lift / press and twist swapped places: in the 3x2 panel the top row holds only pushes, the bottom row the tilts and
  the twist.
- Every chip starts with a marker for the kind of motion (a flat bar for a push, a wedge for a tilt, a turn arrow for
  the twist).
- A key action shows its keys instead of the word "Keys" (`Left / Right`, `Ctrl+Page up / Ctrl+Page down`), and a zoom
  action shows the keys of the profile it is shown for (`Ctrl+Wheel`), so two motions with keys can be told apart.

### One Restart item (#168)

"Restart daemon" and "Restart interface" became one **Restart** item (tray and Overview). It releases held keys,
restarts the daemon and then the interface; a failed daemon restart does not stop the interface restart.

### Safer helper programs (#150)

The interface starts `systemctl` and `launchctl` (and `powershell` in the scripts) only from fixed directories
instead of through `PATH`, as the daemon already did. A helper that is not found is an error, with no fallback to
`PATH`. See [security.md](security.md).

### Build and release pipeline (#149)

The CI images are pinned by digest, the Qt installer (aqtinstall) is installed with pinned hashes, and the release
artifacts carry a build provenance attestation (`gh attestation verify`, see [releasing.md](releasing.md)). A weekly
job proposes a pull request that refreshes the pins.

<!-- RELEASE BLOCKER: the visual joystick mode editor (#143) is added here when it is merged; delete this comment then. -->

## Not verified

The interface changes were built and self-tested offscreen by CI on Linux, Windows and macOS; they were not looked at
on a real desktop. Scroll direction with `INVERT_AXES`, the zoom key on macOS, the tray Restart and the look of the
new chips need a human on real hardware.
