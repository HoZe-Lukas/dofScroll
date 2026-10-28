<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 1.3.0 (draft)

> **Draft, written 2026-10-28 from the merged pull requests since 1.2.3 (#203 to #278) and from the code of `main`.
> Nothing here is final.** The release number is open: `docs/status-release-1.2.4.md` planned 1.2.4, this draft assumes
> 1.3.0 because the configuration layout and the profile keys grow. Before the release the maintainer checks every
> statement against the code (`docs/releasing.md`), fills the credits of the pull requests merged after this draft, and
> checks the wording of the "Not verified" and "Known limitations" sections. Items under "Still planned" are **not** in
> `main` and must not be announced as features.

> **Tested extensively only on RHEL 9** (X11, KDE Plasma, SpaceMouse Pro and Compact). Windows: first tests on one real
> machine (SpaceMouse Pro, official 3Dconnexion driver). macOS: built and unit-tested by CI, never run with a real device.
> Everything new in this release was developed without a SpaceMouse at hand: see "Not verified".

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
- **Take over and New profile.** Copy the action (optionally the tuning) of a motion or button from another profile or
  mode; build a whole profile from parts of others.
- **Key picker as a tree in columns**, and a reworked axis assistant that starts and ends steps by your movement.
- Live feed names the device of each sample, so the Axes page and the assistant can follow one of several devices.
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
- Pointer movement exists in the output layer for all four backends (`Output::moveMouse`), but **no action uses it yet**
  (see "Still planned").

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
- `dofscroll-ui --self-test` skips the info popover checks on Wayland (#212 by @codingkoala, part 1 of #193; #213)
- CI: pins in `packaging/ci/images.env`, monthly refresh without a token (#205); refresh opens the PR and approves its
  checks (#207, #210; the bot PRs #209, #214); `ci-image` ignores the pins file (#211); shorter apt attempts (#223); the
  refresh split into a read-only and a write job (#278, issue #262)
- Tests: coverage gaps closed (#251)
- German texts use "du", checked by `scripts/check-german-du.sh` (#208)
- Docs and rules: release rule for docs and issues, readme status (#203); wiki and website status after 1.2.3 (#204);
  plans (#231, #238); release status (#233); website accessibility audit and a clearer first screen (#261)

## Thank you

Thanks to @codingkoala for the self-test fix on Wayland (#212) and for the reports with logs on Ubuntu 26.04 (#191,
#192, #193) that led to 1.2.3.

## Not verified

None of this was run with a real SpaceMouse in this release cycle. Only a human on real hardware and a real desktop can
verify:

- Migration of a real configuration and the first start on a fresh account on Linux, Windows and macOS; the installer
  upgrade on Windows and macOS.
- The Qt dialogs on a real desktop (import and export file dialogs, take over, New profile, key picker; large fonts, dark
  mode, high-DPI). CI renders them offscreen only.
- Zoom from a tilt (feel, direction), analog and digital keys (feel, thresholds, crosstalk on tilt with digital keys), the
  lift (misfires when grabbing).
- The device selector with two real devices.
- `moveMouse` on every backend (not wired to any action, only compiled and unit-tested on Linux); the macOS part
  (`mac_output.cpp`) has never been run on a Mac (CI only compiles it).
- Wayland, the Windows and macOS paths of everything above (CI compiles and unit-tests them).

## Known limitations

- `dofscroll-ui --self-test` can still fail on a real Wayland session at the font-dependent default size check
  (issue #193); the grabbing-popup problem is worked around, the Qt side (wayland SHM overflow) is not fixed.
- The driver cleanup is available on Windows only (issue #184).
- A pinned idle device on the Axes page shows its last values until it moves.
- Media keys are not sent on macOS.

## Still planned (not in this release unless merged before it)

From [plan-motion-functions.md](plan-motion-functions.md) (issues #224, #225): stages 4 to 7. The motion actions
`pointer_x` / `pointer_y` and `MOUSE_SPEED` (stage 5), `click` / `rightclick` / `middleclick` and `hold:` on a motion
(stage 4), presets such as "turn pages", "switch tabs", "volume" and "change track" (stage 6), the `macro:` function
with a recording dialog, own functions in profiles (`New function...`, the `FUNC_<name>` table), raised size limits (profile
file 1 MiB, action text 32 KiB, 10 000 keys per section) and `TWIST_GUARD` (stage 7). Today the limits are the old ones.
