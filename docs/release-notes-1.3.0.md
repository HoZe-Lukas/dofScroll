<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.3.0

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Windows: first tests on one real
> machine (SpaceMouse Pro, official 3Dconnexion driver). macOS: built and unit-tested by CI, never run with a real device.
> **Nothing in this release was tested with a SpaceMouse in hand**: see "Not verified".

Release after [1.2.3](release-notes-1.2.3.md). The profile pages, the way profiles are stored and what a motion can do
change a lot; the configuration stays readable by the daemon in both layouts.

**Highlights**

- **One file per profile.** Device profiles live in `devices/<name>.conf`, program profiles in `apps/<name>.conf`;
  `dofscroll.conf` keeps `[global]`. The interface migrates an existing single file once, with a backup, and writes only
  the file that changed. Profiles have an optional `PRIORITY` that orders them.
- **Lift is the sixth motion**, and every motion can have its own action in a profile (`V_ACTION`, `H_ACTION`,
  `TWIST_ACTION`, the four push and tilt parts, `LIFT_ACTION`), edited per profile as inherited, set or cleared.
- **Key engine.** Every motion can press a key or chord per direction, analog (`keys:`, repeats faster with more
  deflection, with lowest and highest rate) or digital (`step:`, one press per push, with an own trigger level).
- **Mouse clicks and holds on every motion.** `step:` takes `click`, `rightclick` and `middleclick` per side; the new
  `hold:<neg>,<pos>` holds a key, chord or mouse button while the motion is deflected.
- **Take over and New profile.** Copy the action (optionally the tuning) of a motion or button from another profile or
  mode; build a whole profile from parts of others.
- **Key picker as a tree in columns**, and a reworked axis assistant that starts and ends steps by your movement.
- Live feed names the device of each sample, so the Axes page and the assistant can follow one of several devices.
- Security and robustness: the macOS binaries are signed with the hardened runtime, the packages never enable `spacenavd`,
  the interface flushes pending edits on SIGTERM and Restart and shows foreign text (profile names, tooltips) as plain
  text; the release job gates on `main`, green checks and an approval.
- CI: the monthly pin refresh no longer runs package code next to a write token; pins live in `packaging/ci/images.env`.

## One file per profile

Plan and rules: [plan-config-split.md](plan-config-split.md).

- Layout: `dofscroll.conf` (`[global]`), `devices/<slug>.conf` (exactly one `[device:NAME]`), `apps/<slug>.conf` (exactly
  one `[app:NAME]`), `modes/` unchanged. The file name is a canonical slug of the profile name (Windows reserved names,
  invalid UTF-8 and path characters are handled; a collision gets a hash suffix).
- **Order.** Files have no order of their own, so a profile may carry `PRIORITY` (integer, -1000 to 1000, default 0, no
  effect in `[global]`). Profiles of one kind are applied by `PRIORITY` ascending; the later one wins. A single file
  without `PRIORITY` resolves as before.
- **Migration** (interface, first start of this version, once): the old `dofscroll.conf` is copied byte for byte to
  `backup/dofscroll.conf.pre-split` (a different earlier copy makes the name `.pre-split.1`, ...), every profile is
  written to its own file with `PRIORITY` 10, 20, 30, ... in the old order, the result is read back and compared
  (keys, values, resolved settings), and only then `dofscroll.conf` is replaced by its non-profile part. On any
  difference the new files are removed and the old file stays. A symlinked `dofscroll.conf` keeps the single-file format.
  The daemon never migrates; it reads both layouts and reloads when a file changes.
- The interface writes only what changed, refuses to overwrite or delete a file that changed on disk by hand (it
  reloads and says so next to the profile), lists defective files as own entries, and offers Move up / Move down, rename,
  delete, **Export** (a file copy) and **Import**.
- **Import** reads a profile file and checks it: it strips `PRIORITY`, `ENABLED`, `BACKEND`, `TICK_HZ` and `INPUT`,
  asks before accepting short names or key chords with ctrl, alt or super, and a risky binding (a key that could do
  damage, a click, a Button mode that is not built in) is shown. **An imported profile is applied before your own
  profiles**, so yours win where both set the same key.

## Profiles: every motion, every button

- New optional keys in `[global]`, `[device:NAME]` and `[app:NAME]`, written only when set: `V_ACTION`, `H_ACTION`,
  `TWIST_ACTION`, `V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`, `LIFT_ACTION`. Same grammar as in
  joystick mode files. Each motion in a profile is *inherited*, *set here* or *cleared here* (**Clear**, **Switch on**,
  **Use the inherited setting**); `BUTTON_<n> = none` is an explicit unbind.
- Any action can be given to any motion (zoom from a tilt is one example). The editor explains the choice in words and
  greys out choices that cannot work in that profile. Zoom from a split part pauses the other wheel motions.
- **Lift** (`y` on Linux, `z` on the Windows and macOS defaults, taken from `LIFT_MIX`) has `LIFT_ACTION` (`none` and
  `default` mean the same, `wheel_v`, `wheel_h`, `zoom`, `step:`; an analog `keys:` is refused because the cap moves when
  you grab it) and the shaping keys `LIFT_MIX`, `LIFT_SENS`, `LIFT_DEADZONE` (default 80), `LIFT_SIGN`, `LIFT_CURVE`,
  `LIFT_SMOOTH_MS`.
- `[global]` and `[device:*]` are two layers now: a `[device:*]` `V_ACTION` (or `H_ACTION`) drops a `[global]` push/tilt
  split of the same channel, as the code comment always said.
- The separate **Buttons** tab of the Profiles page is gone; everything is on **Motions and buttons**. The settings search
  no longer filters button rows (they are shown one at a time via the picture).
- **Take over from ...** next to Clear on every motion (push and tilt separately) and every button, and in the Joystick mode
  editor: copies the effective assignment of another profile, joystick mode or (for buttons) Button mode as explicit keys.
  A copy, no link. The check box **Also take over the tuning** (off by default) copies sensitivity, dead zone, direction,
  curve and smoothing too.
- **New profile...** builds a profile: kind, name, base, joystick mode, Button modes (by name or own copy), single motions
  and buttons taken over from others.

## Keys on every motion: analog and digital

- Each motion (vertical and horizontal push and tilt, twist, lift; both directions) can press a key or chord per
  direction. **Analog** `keys:<neg>,<pos>` repeats faster with more deflection; **digital** `step:<neg>,<pos>` presses once
  when the deflection crosses the trigger and re-arms at half of it. `none` is allowed per side
  (`keys:none,pagedown`, `step:up,none`), at least one side needs a key.
- New keys per motion prefix `P` (`SCROLL_V_PUSH_`, `SCROLL_V_TILT_`, `SCROLL_H_PUSH_`, `SCROLL_H_TILT_`, `ZOOM_`, `LIFT_`):
  `<P>KEY_RATE_MIN`, `<P>KEY_RATE_MAX`, `<P>TRIGGER`. They are valid in profiles and joystick mode files. The Profiles page
  and the joystick mode editor have a check box **Analog** (it only switches `keys:` and `step:`) and fields for the
  lowest and highest rate and the trigger level.
- With default parameters the output is the output of 1.2.3 (golden-master test `tests/test_golden_motions.cpp`), except
  for the changes listed below.
- **Mouse buttons and holds** (stage 4 of issue #224). `step:<neg>,<pos>` accepts `click`, `rightclick` and
  `middleclick` per side (one click per crossing of the trigger). `hold:<neg>,<pos>` holds a key, a chord (modifiers alone
  are allowed) or a mouse button while the motion is deflected beyond the trigger and lets go at half of it. Digital only;
  held modifiers are the union of all holds, button bindings and the zoom modifier. A hold longer than 60 s is logged once
  and never released by a timer. Every held state is released on a mode switch, reload, suspend, `ENABLED=0`, device
  removal, a stale source and the safety net (`tests/test_stuck_keys.cpp`). The profile and joystick mode editors offer
  "Mouse click per push" and "Hold a key or button while pushed" in words.
- Lift fix: the lift filter and key state no longer survive a switch to the normal path, and no lift step fires when a
  twist zoom ends while the cap is lifted.
- Pointer movement exists in the output layer for all four backends (`Output::moveMouse`), but **no action uses it on
  `main` yet** (see "Not on main yet").

## Axis assistant and Axes page

- A step starts when you move the cap and ends when you moved it out and back and let go; a valid result is shown for one
  second and the next step starts. Optional tilt steps skip themselves after 5 seconds without movement. Next, Back and
  Measure again stay. The thresholds can be tuned in `ui.conf` (`axis_assistant_start_threshold`, `_rest_ms`, `_max_s`,
  `_auto_skip_s`; documented in [ui.md](ui.md)).
- The lift/press warning follows the axis convention of the system (Linux order; the Windows driver order) and no longer
  names an axis.
- Live feed: a new frame type tells which device a sample comes from (parsers that do not know it skip it). The Axes page and
  the assistant get a **Device** selector when two or more devices are connected. By design the daemon sends the device that
  moved last, so a pinned idle device shows its last values until it moves again.

## Security, packages and robustness

- **macOS: hardened runtime** (#292, issue #269). The DMG is signed ad hoc with `--options runtime`, inside out and without
  `--deep`, so `DYLD_INSERT_LIBRARIES` is ignored by the interface and the probe. **The daemon keeps the entitlement
  `com.apple.security.cs.disable-library-validation`** because it loads the vendor framework
  `3DconnexionClient.framework`; it is therefore **not protected against DYLD injection, the same as on Windows**
  (see [security.md](security.md)). The signature changes, so macOS asks for the Accessibility and Input Monitoring
  grants again after the update. Still not signed with a developer ID and not notarized (issue #42).
- **Packages** (#286, issue #264). The RPM and DEB scripts never enable or start `spacenavd` (they print the command to
  run). A dangling tray link in a home directory is removed as that user, not as root. New `prerm`: on removal only,
  the units of every logged-in user are stopped and disabled.
- **Release job** (#287, issue #263). The `release` job runs `scripts/release-gate.sh` first (the tagged commit must be on
  `main`, every required check green), waits for the approval of the environment `release`, and publishes a draft
  release with all files (needed for immutable releases). The repository settings are set by hand:
  [release-checklist-1.3.0.md](release-checklist-1.3.0.md).
- **Qt archives** (#291, issue #265). Every Qt installation verifies the downloaded archives against
  `packaging/ci/qt-archives.sha256`; **the list is still empty** until the workflow `qt-archive-hashes` has run once
  (issue #295). A weekly workflow warns when an advisory fixes a bundled library.
- **Interface** (#290, #299, #288, #297). Pending edits are written on SIGTERM, SIGINT and SIGHUP and before Restart;
  an export replaces a file atomically and refuses links and folders; a rename flushes the editor first. Labels, tooltips
  and message boxes show foreign text (profile, device and mode names, file errors) as plain text, and bidi and
  zero-width characters are stripped from status names. The first-run guide changes **Start at login** only when you
  press **Finish**. The default window is wider (1800x1100, clamped to 92% of the screen) and the list/editor splitters
  are proportional.
- **Audits** (#279, #280, #282, #294, #267). Helper lookup, socket timeout, close-on-exec, no-follow file access, the
  session fallback and HID ids; low supply-chain findings (`persist-credentials: false`, hardening check on more
  binaries, uninstall only below the prefix); a fuzz driver for the profile loader and the migration (ASan/UBSan).

## Backward-incompatible changes

- **A program's `BUTTON_<n>` now beats the joystick mode's binding for that button.** Before, the joystick mode won. If a
  program profile bound a button that the active joystick mode also binds, the program's binding now applies.
- **The configuration is migrated** to one file per profile at the first start of the interface (see above). An older
  dofscroll (1.2.3 and earlier) reads only `dofscroll.conf`, so after the migration it sees no profiles. Downgrade by hand:
  `cat devices/*.conf apps/*.conf >> dofscroll.conf` (append to the `[global]` file) or restore
  `backup/dofscroll.conf.pre-split`. A symlinked `dofscroll.conf` is not migrated.
- **New profile keys** (`V_ACTION` ... `LIFT_ACTION`, `LIFT_*`, `<P>KEY_RATE_MIN`/`KEY_RATE_MAX`/`TRIGGER`, `PRIORITY`) are
  ignored with a warning by an older daemon.
- **An analog `keys:` with a key that is not safe to repeat is refused** by the parser ("this key can only be used
  digitally, use step:"): letters, digits, function keys, Enter, Delete, Backspace, Esc, Space, Alt or Super combinations,
  play/pause, a lone modifier. Allowed analog: navigation keys, ctrl or shift plus a navigation key, volume, media
  next/previous, plus and minus. `step:` stays allowed with the plain load warning. **The built-in Media mode now uses
  `TWIST_ACTION = step:j,l`** (one jump per twist) instead of repeating `j` and `l`; a copy of the old file with `keys:j,l`
  must be changed to `step:j,l`.
- **At most 4 key presses per tick and function, and 8 per tick in total** (it was 200). A very high rate no longer
  produces a burst; the analog hard cap is `min(100, TICK_HZ/2)` presses per second.
- Settings search on the Profiles page no longer filters button rows.
- **Packages no longer enable `spacenavd`.** If you relied on the package to start it, run
  `sudo systemctl enable --now spacenavd.service` once.
- **macOS: the signature changes** (hardened runtime), so the Accessibility and Input Monitoring grants have to be given
  again after the update (see above).
- The default window of the interface is larger.

## All changes

Credits: pull requests by the maintainer unless marked.

- Config split: profile files loader and writer (stages 1 and 2, #218), migration and seeding (stage 3, #220), the
  interface writes one file per profile (stage 4, #222); plans #216, #219; review and audit fixes (#232, #237)
- Profiles: clear, switch on and inherit motions and buttons (#215); Buttons tab removed (#206); take over, any action on
  any motion, New profile (#221); take over also the tuning (#256, issue #253); dialog and accessibility fixes (#277)
- Motions: lift as the sixth motion, global and device as two layers (#257, #252); key engine (#259); golden-master test
  (#234)
- Key picker as a tree in columns (#255)
- Axis assistant start and end by movement (#235, issue #190); lift/press warning (#248, issue #186); live feed device id
  and selector (#239, issue #139)
- Output: relative pointer movement, all backends (#236); macOS clamps to all displays and drags while a button is held,
  security note on pointer motion, Button mode risk check (#267)
- Mouse click and hold on every motion, lift audit fix (stage 4 of #224, #283)
- Security and robustness: I/O audit round 3 (#279); supply-chain low findings (#280, part of #266); fuzz driver for the
  profile loader (#282, #294); packages never enable `spacenavd` (#286, issue #264); release gate, environment `release`
  and tag creation rule (#287, issue #263; #296); first-run guide changes Start at login only on Finish (#288, issue #276);
  UI data loss on signals, export and rename (#290, issue #273); Qt archive hashes and the advisory check (#291,
  issue #265); macOS hardened runtime (#292, issue #269); plain text for foreign strings (#299, issues #274, #240)
- UI: proportional splitters and a wider default window (#297); accessible names, undo for Remove profile and Apply (#271)
- `dofscroll-ui --self-test` skips the info popover checks on Wayland (#212 by @codingkoala, part 1 of #193; #213)
- CI: pins in `packaging/ci/images.env`, monthly refresh without a token (#205); refresh opens the PR and approves its
  checks (#207, #210; the bot PRs #209, #214); `ci-image` ignores the pins file (#211); shorter apt attempts (#223); the
  refresh split into a read-only and a write job (#278, issue #262)
- Tests: coverage gaps closed (#251)
- German texts use "du", checked by `scripts/check-german-du.sh` (#208)
- Docs and rules: release rule for docs and issues, readme status (#203); wiki and website status after 1.2.3 (#204);
  plans (#231, #238); release status (#233); website accessibility audit and a clearer first screen (#261); statements
  that contradicted the code (#289); the draft of these notes (#284)

## Thank you

Thanks to @codingkoala for the self-test fix on Wayland (#212) and for the reports with logs on Ubuntu 26.04 (#191,
#192, #193) that led to 1.2.3.

## Not verified

**None of this was run with a real SpaceMouse in this release cycle.** Only a human on real hardware and a real desktop
can verify these; none of them has been tested:

- Scrolling, zoom, buttons, the pointer path (`moveMouse`) and the tray with a real device, on every system. This includes
  the feel of analog and digital keys (thresholds, crosstalk on tilt with digital keys), zoom from a tilt, click and hold
  (trigger and half-level re-arm, a drag with a hold on macOS, uinput and Windows), and the lift (misfires when grabbing).
- Real Windows, macOS and Wayland sessions for everything above and below. CI compiles and unit-tests those paths and
  runs the interface offscreen; it never ran a real session. `moveMouse` is compiled and unit-tested on Linux only;
  `mac_output.cpp` has never run on a Mac.
- **macOS, hardened runtime.** The behavior with System Integrity Protection on, and the bundled Qt frameworks and
  plugins under the hardened runtime, are untested on a real Mac; only the CI runner loaded them. That the daemon still
  loads the vendor framework with the driver installed is checked by `diagnostics/test-driver-coexistence.sh` on a Mac.
  **The macOS daemon keeps `disable-library-validation` and is not protected against DYLD injection, the same as the
  Windows daemon.**
- Migration of a real configuration and the first start on a fresh account on Linux, Windows and macOS; the installer
  upgrade on Windows and macOS; a real RPM and DEB install, upgrade and removal with the new scriptlets.
- The Qt dialogs on a real desktop (import and export file dialogs, take over, New profile, key picker, setup guide; large
  fonts, dark mode, high-DPI, screen readers); the device selector with two real devices; the axis assistant thresholds.
- The `release` job against the real GitHub API (gate, environment approval, draft then publish, immutable release):
  it has never run on a tag.

## Known limitations

- `dofscroll-ui --self-test` can still fail on a real Wayland session at the font-dependent default size check
  (issue #193); the grabbing-popup problem is worked around, the Qt bug is not fixed (issue #258).
- The driver cleanup is available on Windows only (issue #184).
- A pinned idle device on the Axes page shows its last values until it moves (issue #245).
- Media keys are not sent on macOS.
- Windows and macOS packages are unsigned (issue #42); the Qt archive hash list is empty until it is recorded (issue #295).
- The macOS and Windows daemons load the vendor library and are not protected against DYLD or DLL injection.
- Open from the audits and not in this release: daemon core and parser hardening (issue #272), the remaining UI
  robustness findings (issue #275), systemd unit shadowing (issue #270), the Motion help naming Linux axes (issue #249).

## Not on main yet

These were planned for 1.3.0 but are **not merged at the time of writing**. They must not be announced until they are; this
section is finalized (moved up or removed) before the tag.

From [plan-motion-functions.md](plan-motion-functions.md) (issues #224, #225), stages 5 to 7: the motion actions
`pointer_x` / `pointer_y` and `MOUSE_SPEED` (stage 5), presets such as "turn pages", "switch tabs", "volume" and "change
track" (stage 6), the `macro:` function with a recording dialog, own functions in profiles (`New function...`), raised
size limits and `TWIST_GUARD` (stage 7). Also pending: parser hardening (issue #272), UI robustness (issue #275), and the
pull request that flags the user's own Button modes as risky and copies the key engine keys when taking over the tuning
(#298, issues #268 and #260). Today the size limits are the old ones.
