<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.3.3

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Windows: tests on one real
> machine (SpaceMouse Pro, official 3Dconnexion driver); nothing of this release was run there. macOS: built and
> unit-tested by CI, never run with a real device. See "Not verified".

Small release after [1.3.2](release-notes-1.3.2.md): one change in the interface and two fixes in how the daemon is
stopped. The configuration and the profile files do not change.

**Highlights**

- **The key capture field shows the key or combination that is set.** Wherever a key press is chosen (a macro step, the
  New function window, button actions) the field used to be a button with a fixed text. It now shows the key or
  combination that is set (for example "Ctrl+Shift+Page down", with the translated key names); click it and press the new
  combination, Esc cancels. "Press a key..." is shown only while nothing is set. The field stays in step with the
  modifiers and the key list next to it.
- **Windows: the daemon receives log-off and shutdown (#358, #376).** The daemon's hidden window was a message-only
  window, which gets no `WM_QUERYENDSESSION` or `WM_ENDSESSION`, so the early release of held keys at log-off never ran.
  It is now a normal top-level window of size 0 that is never shown, with the same message handling. The console control
  handler is unchanged.
- **systemd: a duplicate instance no longer releases the running instance's keys (#359, #377).** A second start (for
  example the unit while a daemon started by hand holds the instance lock) exits with status 75, but `ExecStopPost` still
  ran `--release-keys`, which sent key-ups that released keys the first, healthy daemon was holding. The unit now skips the
  release after exit status 75; after a crash, a kill or a normal stop it runs as before. `dofscroll --release-keys` and the
  tray's "Release all keys" are unchanged.

**Documentation and checks**

- The website screenshots (English and German) are rendered again with `scripts/make-site-screenshots.sh`; the German ones
  show German key names and the informal "du" (#374, issue #344).
- Repository and CI: `scripts/configure-repo.sh` is removed and `docs/repo-settings.md` describes the live settings, which
  are made by hand (#371, issue #362); tags are created by hand by the administrator (#370, issue #361, in 1.3.2 already);
  the `codeql-action` actions are at 4.39.3 (#373).
- `package_layout` checks the new unit guard, `RestartPreventExitStatus=75` and that the exit status of a duplicate
  instance is still 75 (#377).

## Not verified

- **Nothing was tested with a SpaceMouse in hand.**
- **Windows log-off (#358):** the change was compiled by CI and the unit tests pass; nobody logged off or shut down a
  real Windows machine with a key held and read `dofscroll.log`. The issue stays open until someone has.
- **systemd (#359):** the guard was run by hand in a shell with `EXIT_STATUS` 75, 1 and unset, and `package_layout` checks
  the rendered text, but the unit was not started under real systemd. How systemd treats the escaped `$` and the quoted
  `sh -c` word under the unit's sandbox (`PrivateUsers`, `SystemCallFilter`) is untested; `/bin/sh` is assumed to exist. The
  scenario itself (a second start while another daemon holds a key) needs a live X11 session.
- **The key capture field** was checked by the offscreen self-test only, not on a real desktop. Whether every key reaches
  the field depends on the desktop: system shortcuts such as Alt+Tab or the Windows key are taken by it.
- **The screenshots** were rendered offscreen, not on a real desktop.
- Everything listed under "Not verified" in [1.3.2](release-notes-1.3.2.md) and [1.3.1](release-notes-1.3.1.md) still
  holds.

## Known limitations

- A running macro survives a change of the active program (issue [#318](https://github.com/HoZe-Lukas/dofScroll/issues/318)).
  This stays as it is by design: a started macro always runs to its end.
- The device files miss two ids that `spacenavd` knows (the wireless receiver `256f:c652` and the Module `256f:c636`,
  issue [#356](https://github.com/HoZe-Lukas/dofScroll/issues/356)). No id is added without a verified source.

## Backward-incompatible changes

None.

## All changes

Credits: pull requests by the maintainer unless marked.

- Key capture field shows the key or combination that is set (#375)
- Windows: `WM_ENDSESSION` in a hidden top-level window (#376, issue #358)
- systemd: no `--release-keys` after the duplicate-instance exit (#377, issue #359)
- Website: screenshots rendered again (#374, issue #344)
- Repository and CI: `configure-repo.sh` removed (#371, issue #362); `codeql-action` bumps (#373)
