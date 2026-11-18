<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.3.4

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Windows: tests on one real
> machine (SpaceMouse Pro, official 3Dconnexion driver); nothing of this release was run there. macOS: built and
> unit-tested by CI, never run with a real device. See "Not verified".

Small release after [1.3.3](release-notes-1.3.3.md): two changes in the interface. The configuration and the profile
files do not change.

**Highlights**

- **Driver cleanup: every program you leave to the driver gets an empty profile.** After the cleanup assistant (Windows
  and macOS) the profiles page offered an empty profile (`ENABLED = 0`) only for 3D and CAD programs. It now lists every
  program that you did not tick and that has an executable name, ticked by default, so dofscroll does not scroll or zoom
  there on top of the driver and the program works without adding it by hand. A program that already has a profile is
  shown and never changed; nothing is created that is not ticked. The page, its texts (English and German) and the setup
  guide step say "programs left to the driver".
- **The key capture field is the same on every place that takes a key.** The key picker with the modifier boxes and the
  **Press a key...** field are now the two ways to set a key or a combination in the keys of a motion (profile and
  joystick mode), the lift and press action, the key of a button action (profile, Button mode, joystick mode), the steps
  of a macro and the New function window. Esc cancels, a modifier alone waits for its key, a key dofscroll cannot send is
  refused in words.

## Not verified

- **Nothing was tested with a SpaceMouse in hand.**
- **The profiles page** was changed in the model and its text; the unit test covers the rows, but the cleanup wizard was
  not run on real Windows or macOS.
- **The key capture field** was not run on a real desktop; the Qt views were compiled only where Qt was available. Whether
  every key reaches the field depends on the desktop (system shortcuts such as Alt+Tab are taken by it).
- **The screenshots** on the website were not rendered again for these changes.
- Everything listed under "Not verified" in [1.3.3](release-notes-1.3.3.md), [1.3.2](release-notes-1.3.2.md) and
  [1.3.1](release-notes-1.3.1.md) still holds.

## Known limitations

- A running macro survives a change of the active program (issue [#318](https://github.com/HoZe-Lukas/dofScroll/issues/318)).
  This stays as it is by design: a started macro always runs to its end.
- The device files miss two ids that `spacenavd` knows (the wireless receiver `256f:c652` and the Module `256f:c636`,
  issue [#356](https://github.com/HoZe-Lukas/dofScroll/issues/356)). No id is added without a verified source.

## Backward-incompatible changes

None.

## All changes

Credits: pull requests by the maintainer unless marked.

- Driver cleanup: empty profiles for every program left to the driver
- Key capture field on every place that takes a key or a key combination
