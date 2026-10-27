<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Plan: any function on any motion, analog or digital, and own functions in profiles

Status: specification (read-only design of 2026-10-27), waiting for the maintainer's decisions in "Open decisions".
Nothing of it is implemented. Tracking issues: #224 (any function on any motion), #225 (own functions in profiles).
It builds on the merged profile keys (`V_ACTION`, `H_ACTION`, `TWIST_ACTION`, `V_PUSH_ACTION`, `V_TILT_ACTION`,
`H_PUSH_ACTION`, `H_TILT_ACTION`, `src/profile_actions.hpp`) and on the split configuration (`docs/plan-config-split.md`).

## Wishes (maintainer)

- Every function can be put on every motion (push and tilt separately, twist, lift; both directions), in profiles and
  in joystick modes, with analog control.
- A key-like function (for example an arrow key on the joystick) has a switch analog / digital: analog presses the key
  more often with more deflection, digital presses it once, like a click. Device buttons always stay digital.
- New functions are in scope too (pointer, volume, tab switching, page up/down, any key or chord, clicks, holds).
- Own functions can be defined directly in a profile, without creating a joystick or button mode first.

## What exists today

- `keys:<neg>,<pos>` is analog: the repeat rate is the channel rate (depth times SENS, curve applied) in the
  `StepAccumulator`; the first press comes after 1/rate. `step:<neg>,<pos>` is digital: one press when the rate reaches
  `STEP_TRIGGER` (default 20, global), re-armed at half the threshold, state per side. The switch is therefore
  `keys:` versus `step:`; missing are the UI, rate limits and per-motion settings.
- Five of six motions have actions (V/H split into push and tilt parts, twist = `ZOOM_*`). Lift/press (`y`) has none.
  Shaping per motion exists for the four parts (`SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`) and
  twist (`ZOOM_*`); lift and the key parameters have no keys.
- Functions: `default`, `none`, `wheel_v`, `wheel_h`, `zoom`, `keys:`, `step:`. Missing: pointer movement (`Output`
  has no `moveMouse`), mouse click or hold on a motion, hold a key, `none` per direction.
- Tick at `TICK_HZ` (default 250), one `flush()` per tick; safety net frees everything after 250 ms without an
  active axis or key; `tapKey` is atomic, `setKey` and `mouseButton` hold state; the uinput buffer is 4096 events.
- Security (`docs/security.md` section 7): a warning at load (`chordNeedsWarning`, `chordRisk`), no gate; the key list
  is closed (`src/keys.hpp`).

## Model

Six motions: `v_push` (z), `v_tilt` (rx), `h_push` (x), `h_tilt` (rz), `twist` (ry), `lift` (y); each has two
directions. Axis names come from the configuration (`axis_roles_model.hpp`), never hard-coded. Every function is
allowed on every motion.

| Family | Functions | Nature |
|---|---|---|
| Continuous | `wheel_v`, `wheel_h`, `zoom`, `pointer_x`, `pointer_y` | always analog, no switch in the UI |
| Key or chord per direction | `keys:` / `step:` with key, chord or `none` per side | switch analog / digital |
| Mouse button per direction | `click`, `rightclick`, `middleclick`; variant `hold` | digital (tap); `hold` while deflected |
| Hold (key or chord) | `hold:` | digital, held while deflected |

- Digital: value `u` (rate after smoothing, deadzone, curve, SENS); one press when `|u| >= T` and the side is armed;
  re-armed at `|u| <= T/2`; the other direction fires the other key, each side re-arms on its own; `T` is
  `STEP_TRIGGER`, overridable per motion with `<P>_TRIGGER`; no auto repeat.
- Analog: repeat rate `clamp(|u|, rmin, rmax)` with `<P>_KEY_RATE_MIN` (default 0 = no lower limit) and
  `<P>_KEY_RATE_MAX` (default unlimited = today; hard cap `min(100, tick_hz/2)`); the first press comes immediately when
  leaving rest if `rmin > 0` or a trigger is set (accumulator preloaded), otherwise the old delay 1/r stays. One
  algorithm whose default parameters reproduce today's behavior (golden-master test first). UI defaults: rmin 3,
  rmax 25.
- At most 4 presses per tick and function (today 200); change documented in the release notes.
- Conflicts: the zoom latch only applies to twist = `zoom`; for another twist function an optional `TWIST_GUARD = 1`
  (same dominance detection), default off so the built-in `timeline` mode is unchanged; one function per motion; the
  same function on two motions is allowed (wheel and pointer add up, keys tap independently).
- Lift fires when grabbing: UI warning, default `none`, high deadzone, only digital or continuous, never an analog key.

## Function list and safety

- Tier A (everywhere, also analog): wheel, zoom, pointer, navigation keys (arrows, page up/down, home/end, tab),
  ctrl or shift plus navigation chords (ctrl+pageup/pagedown = tab switch), volume, media next/previous, plus/minus.
  UI presets: "turn pages", "switch tabs", "volume", "change track".
- Tier B (digital only, load warning as today): letters, digits, function keys, enter, delete, backspace, escape, space,
  alt or super chords (alt+tab, super+tab), play_pause, mouse buttons, `hold:`.
- Proposed hard rule: `keys:` (analog) with a tier B key is rejected by the parser ("this key can only be used
  digitally, use step:"); digital stays allowed with the warning. To be added to `docs/security.md` section 7.
- Not included: `mode:` switching on motions (stays a device button), free characters, scripts.
- Release of held state on mode switch, reload, suspend, `ENABLED=0`, device removal, stale watchdog and the safety
  net (`releaseAll`); `axesActive()` must include lift.
- Platforms: X11 `XTestFakeRelativeMotionEvent`; uinput `REL_X/REL_Y`; Windows `SendInput` `MOUSEEVENTF_MOVE`; macOS
  `CGEvent` mouse moved or left mouse dragged, clipped to the screen; media keys are not sent on macOS (documented).

## Configuration syntax (compatible)

Stored stays `keys:<neg>,<pos>` (analog) and `step:<neg>,<pos>` (digital); the UI check box only switches the prefix.
New per side: `<chord> | none | click | rightclick | middleclick`; `hold:<neg>,<pos>`; `pointer_x`, `pointer_y`;
`LIFT_ACTION`, `LIFT_MIX` (default `y`; Windows and macOS seeds `z`). New keys (valid in profiles and joystick modes, same
`applyKey` validation): per motion prefix `P` in `SCROLL_V_PUSH_`, `SCROLL_V_TILT_`, `SCROLL_H_PUSH_`, `SCROLL_H_TILT_`,
`ZOOM_`, `LIFT_`: `<P>KEY_RATE_MIN`, `<P>KEY_RATE_MAX`, `<P>TRIGGER`; `LIFT_SENS`, `LIFT_DEADZONE` (default 80),
`LIFT_SIGN`, `LIFT_CURVE`, `LIFT_SMOOTH_MS`; global `MOUSE_SPEED` (pixels per step, default 8) and `TWIST_GUARD`.
All old actions and keys stay valid and produce the same output with default values; old daemons ignore new keys with a
warning.

    # [app:browser]: tab switching analog on sideways tilt, page up/down digital on forward tilt
    H_TILT_ACTION = keys:ctrl+pageup,ctrl+pagedown
    SCROLL_H_TILT_KEY_RATE_MAX = 8
    V_TILT_ACTION = step:pageup,pagedown
    LIFT_ACTION = step:volume_up,volume_down
    H_PUSH_ACTION = pointer_x
    V_PUSH_ACTION = pointer_y
    TWIST_ACTION = hold:click,none

## Own functions in profiles

- Inline is the main case: an own function is simply the value of an action key (`H_TILT_ACTION =
  keys:ctrl+pageup,ctrl+pagedown`, `BUTTON_3 = key:ctrl+0`). "New function..." in the UI is a composition that ends in
  exactly one action text, validated by `parseChannelAction` / `parseButtonAction`. No new file concept.
- New in the action text: a key sequence, steps separated by a space (`step:ctrl+k ctrl+c,ctrl+k ctrl+u`); a space inside
  a side is an error today, so this is compatible.
- Later (only if the UI needs an "Own functions" list): a named local table `FUNC_<name> = <action text>` (name
  `[a-z0-9_-]{1,32}`) used as `func:<name>`; resolved at load time within the same section only (files stay
  independent: copy, exchange, reload of one file); unknown name: warning, line ignored; recursion impossible.
- Split layout: every profile file carries its own action keys and `FUNC_*`; export and import of a file contain them.
- Save as joystick or button mode: creates `modes/<id>.conf` (or `buttons/<id>.conf`) with the keys of that motion; the
  profile keeps its own function (copy, not replace, because replacing can change the precedence program > mode).
  Take over from a mode: copies the action text plus the motion's settings as an own function, no link afterwards;
  one shared copy operation `copyMotionSettings(src, dst, motion)` for profiles and modes (the take-over feature on
  branch `ui/profile-borrow-build`, issue #226, must reuse it); `func:` references are resolved when copying.
- UI: per motion and button a drop-down with the existing functions, then "New function...", "Copy from joystick
  mode...", "Copy from another profile..."; the dialog (name optional, kind: key or combination, key sequence, mouse
  click, hold key, scroll or zoom, move pointer; per direction fields; key capture widget; analog / digital check box only
  for key functions, grey with reason for clicks and sequences; rate and trigger fields with defaults; live preview of the
  action text; no save button, assigned as soon as all fields are valid); a list "Own functions of this profile" (used by,
  edit, duplicate, delete, save as mode); deleting a used function sets its users back to inherited and says so in words;
  inheritance in words (Global < device < mode < program); German with du in `po/de.po`.
- Limits (constants in `config.cpp`, tested): at most 32 `FUNC_*` per section, 512 keys per section, action text at most
  256 bytes, at most 8 steps per side, at most 4 modifiers per step; sequences run at most one step per tick with a queue of
  16 steps per function (overflow is dropped; a new trigger during a sequence is ignored until it is done); at most 4
  presses per tick and function and 8 per tick in total; a hold longer than 60 s is logged once, not released.
- Proposed opt-in for risky sends in own functions: `ALLOW_RISKY_KEYS = 1` in the same section; without it the daemon
  does not load tier B actions of a profile (warning, action = `none`). Only for new actions on motions and for `seq`
  and `func:`; existing `BUTTON_<n> = key:...` and mode files keep the plain warning. Not inherited (only the section
  that holds the action counts); the profile import removes the flag and says so.

## Stages (small PRs, each with tests)

1. Golden master: `test_mode_runner` and `test_scenarios` record the outputs of all existing modes and profiles (V/H/
   twist, parts, `keys:`/`step:`). No code change.
2. Lift: `LIFT_ACTION`, `LIFT_*`, profile layer, runner slot, UI editable, lift in the safety net.
3. Key engine: `KeyRepeater` / `DigitalTrigger`, rate min/max, `none` per side, tier A/B rule, tick cap, UI check box and
   fields; scenario tests (rate grows with deflection, first press, digital exactly one press per crossing, re-arm, no
   double fire on push/tilt crosstalk), `test_stuck_keys` for all release paths.
4. Mouse and hold: `click`, `hold:` with the union of modifiers (zoom plus hold), release tests.
5. Pointer: `moveMouse` in all backends, `pointer_x/y`, `MOUSE_SPEED`; fake output test for sums and signs.
6. Presets, docs, `site/`, `po/`, help texts.
7. Own functions: 7a dialog and inline composition (no new format); 7b `seq` syntax and sequence engine with limits; 7c
   `ALLOW_RISKY_KEYS`, load check, import removes the flag, `security.md`, `check_config` reports unused and duplicate
   functions; 7d take over from mode and save as mode (after the take-over branch is merged); 7e optional `FUNC_` table.
   The refactor of `tickCustom` into six slots stays strictly behind the golden-master test and apart from new functions.

What only a human with a device can verify: feel and thresholds of analog / digital, lift misfires, crosstalk on tilt with
digital keys, pointer speed and acceleration on all four backends, drag with hold (macOS), tab and window switching per
system, volume and media keys, Wayland (uinput `REL_X/REL_Y`), sequences in real programs (timing), the opt-in dialog.

## Open decisions (recommendation first)

1. Reject analog keys with tier B keys in the parser (yes), digital with warning.
2. Threshold as a rate like `STEP_TRIGGER` (yes), the UI shows "about N % of full deflection".
3. One algorithm with defaults equal to today (yes).
4. `TWIST_GUARD` default off (yes).
5. Tap cap 4 per tick (yes, release notes).
6. Pointer first on X11 and uinput, Windows and macOS afterwards with the pointer hidden in the UI until built (the
   maintainer wants it on all systems: then Windows and macOS must be part of the release).
7. Lift default `none`, high deadzone, never an analog key (yes).
8. `tickCustom` refactor strictly behind the golden master (yes).
9. Window and tab switching only as presets per system, no built-in defaults (yes).
10. Own functions: inline first, table only if needed (yes); `ALLOW_RISKY_KEYS` gate only for new motion actions, `seq` and
    `func:` (yes); sequences for device buttons later; copy (not link) when taking over from a mode (yes); the flag is
    never inherited and removed on import (yes).
