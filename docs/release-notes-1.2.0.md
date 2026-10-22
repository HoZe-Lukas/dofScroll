<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.2.0

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Other Linux
> distributions were install-tested in CI containers. The **Windows and macOS builds are built and
> unit-tested by CI but have never been run with a real device.**

Release after [1.1.2](release-notes-1.1.2.md). Two new settings let you adapt the direction of the axes and the
zoom key to what you and your programs need, the tray has one Restart item, and the build and release pipeline is
harder to tamper with. The configuration format only gains keys: existing files keep working unchanged. The
biggest change is the visual joystick mode editor.

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

### Visual joystick mode editor: Motions and buttons (#143, #172, #174)

A new first tab, **Motions and buttons**, in the joystick mode editor shows the cap with its six motions and the
numbered buttons of your device, instead of channels and mixes. Click an item (or use the arrow keys) and choose what
it does in the inspector next to the picture.

- **Big action tiles**: scroll, scroll sideways, zoom, Page up / Page down, arrow keys, other keys, nothing, as in the
  profile. For other keys, **Press a key...** takes the key or combination you press; the key list is complete now
  (keypad, punctuation, system and media keys, and F13 to F24 on every system, F13 to F20 on macOS).
- **Reverse direction** per motion (writes the existing `*_SIGN` keys, also per part of a split channel), "Same as ..."
  and "Swap with ...", lift / press as a weighted mix term with a warning, tuning of a motion as overrides.
  Push and tilt of one channel with the same action are written as before (`V_ACTION`), different actions as the
  split keys; the picture hides "channel" and "split". A channel mix you wrote by hand is never changed.
- **Help where you work**: a live sentence with the real rate ("presses Page up about 3 times a second at full
  deflection"), a **?** popover on every element, templates, hints with a fix button, and a short guided tour on first
  use.
- **Detect by moving** selects the motion you move and the button you press (it pauses the daemon output only while
  it is on); **Try this joystick mode** activates the mode now, with a way back.
- **The same picture in a profile** (tab Motions and buttons, between Buttons and Overrides: what the profile's
  joystick mode does, the profile's own tuning with where each value comes from) and on the Button modes page.
- It writes the same `modes/<id>.conf` files and profile keys as before: nothing in the file format changed, and
  hand-edited files keep working.

`INVERT_AXES` (above) flips a physical axis for every joystick mode in a profile; **Reverse direction** in the editor
reverses one motion of one joystick mode.

## Not verified

The interface changes were built and self-tested offscreen by CI on Linux, Windows and macOS; they were not looked at
on a real desktop. Scroll direction with `INVERT_AXES`, the zoom key on macOS, the tray Restart and the look of the
new chips need a human on real hardware.
