<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Plan: any function on any motion, analog or digital, and own functions in profiles

Status: specification (design of 2026-10-27), maintainer's decisions of 2026-10-28 included (section "Decisions of
2026-10-28"). Implemented so far: `Output::moveMouse` (#236), stage 2 (lift as the sixth motion: `LIFT_ACTION`, `LIFT_*`, [global] and [device:*] as two layers, #252) and stage 3 (the key engine: `src/key_engine.hpp`, `<P>KEY_RATE_MIN` / `<P>KEY_RATE_MAX` / `<P>TRIGGER`, `none` per side, the tier rule, the analog check box and the lift list in the joystick mode editor; the take-over dialog does not copy the three keys yet). Tracking issues: #224 (any function on any motion), #225 (own functions in profiles).
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
- Functions: `default`, `none`, `wheel_v`, `wheel_h`, `zoom`, `keys:`, `step:`. Missing: pointer movement as a motion
  function (the output layer `Output::moveMouse` is merged, #236, but no action uses it), mouse click or hold on a motion, hold a key, `none` per direction.
- Tick at `TICK_HZ` (default 250), one `flush()` per tick; safety net frees everything after 250 ms without an
  active axis or key; `tapKey` is atomic, `setKey` and `mouseButton` hold state; the uinput buffer is 4096 events.
- Security (`docs/security.md` section 7): a warning at load (`chordNeedsWarning`, `chordRisk`), no gate; the key list
  is closed (`src/keys.hpp`).

## Decisions of 2026-10-28

1. Pointer on all systems (X11, uinput, Windows, macOS). `Output::moveMouse` is merged (#236); the release contains
   all four backends.
2. No `ALLOW_RISKY_KEYS` gate anywhere. Own functions and the new motion actions get the plain load warning as today.
3. Analog `keys:` with a tier B key is rejected by the parser; only `step:` (digital) is allowed, with the load warning.
4. Take over (#226, PR #221): the dialog has an option "also take over the tuning" (default: take over the action only).
5. Key combinations in every key field are set with the modifier check boxes (ctrl, shift, alt, super) plus either the
   key drop-down or the "Press a key..." capture, which also takes the modifiers (existing widgets
   `src/ui/qt/key_combo.hpp`, `src/ui/qt/key_capture.hpp`); never free text.
6. A macro function (section "Macro function"), digital only.
7. Generous size limits against hostile or broken files only (section "Limits").

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
- Hard rule (decided): `keys:` (analog) with a tier B key is rejected by the parser ("this key can only be used
  digitally, use step:"); digital stays allowed with the warning. To be added to `docs/security.md` section 7. There is
  no further gate: the plain load warning (`chordNeedsWarning`, `chordRisk`) is the only protection for tier B keys.
- Not included: `mode:` switching on motions (stays a device button), free characters, scripts.
- Release of held state on mode switch, reload, suspend, `ENABLED=0`, device removal, stale watchdog and the safety
  net (`releaseAll`); `axesActive()` must include lift.
- Platforms (all four, decided): X11 `XTestFakeRelativeMotionEvent`; uinput `REL_X/REL_Y`; Windows `SendInput`
  `MOUSEEVENTF_MOVE`; macOS `CGEvent` mouse moved or left mouse dragged, clipped to the screen; `Output::moveMouse` exists
  (#236); media keys are not sent on macOS (documented).

## Configuration syntax (compatible)

Stored stays `keys:<neg>,<pos>` (analog) and `step:<neg>,<pos>` (digital); the UI check box only switches the prefix.
New per side: `<chord> | none | click | rightclick | middleclick`; `hold:<neg>,<pos>`; `pointer_x`, `pointer_y`;
`macro:` (section "Macro function");
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
- New in the action text: the `macro:` function (section "Macro function"); it replaces the earlier idea of a `seq` syntax
  with space-separated steps.
- Later (only if the UI needs an "Own functions" list): a named local table `FUNC_<name> = <action text>` (name
  `[a-z0-9_-]{1,32}`) used as `func:<name>`; resolved at load time within the same section only (files stay
  independent: copy, exchange, reload of one file); unknown name: warning, line ignored; recursion impossible.
- Split layout: every profile file carries its own action keys and `FUNC_*`; export and import of a file contain them.
- Save as joystick or button mode: creates `modes/<id>.conf` (or `buttons/<id>.conf`) with the keys of that motion; the
  profile keeps its own function (copy, not replace, because replacing can change the precedence program > mode).
  Take over from a mode: copies the action text plus the motion's settings as an own function, no link afterwards;
  one shared copy operation `copyMotionSettings(src, dst, motion)` for profiles and modes (the take-over feature on
  branch `ui/profile-borrow-build`, issue #226, PR #221, must reuse it); `func:` references are resolved when copying.
  The take-over dialog has the option "also take over the tuning" (the `SCROLL_*` / `ZOOM_*` / `LIFT_*` shaping and the
  rate and trigger keys of that motion); default off: only the action text is copied.
- UI: per motion and button a drop-down with the existing functions, then "New function...", "Copy from joystick
  mode...", "Copy from another profile..."; the dialog (name optional, kind: key or combination, mouse
  click, hold key, macro, scroll or zoom, move pointer; per direction fields; every key field is built from the modifier
  check boxes ctrl, shift, alt, super plus the key drop-down or the "Press a key..." capture, never free text; analog /
  digital check box only for key functions, grey with reason for clicks and macros; rate and trigger fields with defaults; live preview of the
  action text; no save button, assigned as soon as all fields are valid); a list "Own functions of this profile" (used by,
  edit, duplicate, delete, save as mode); deleting a used function sets its users back to inherited and says so in words;
  inheritance in words (Global < device < mode < program); German with du in `po/de.po`.
- Limits: see section "Limits".

## Macro function

Replaces the earlier `seq` idea (stage 7b). A macro is a recorded or typed list of steps that runs once per trigger.

- Digital only: one run per trigger; further triggers during a run are ignored until it is done. The analog / digital
  check box is greyed with the reason ("a macro always runs once per trigger"). Valid on a device button and on a
  motion direction (digital trigger as in "Model").
- Steps: key DOWN, key UP (separate steps), PAUSE (milliseconds). There is no "tap" step: pressing and releasing a key
  is simply two steps. Pauses are editable afterwards.
- Release guarantee: at the END of every macro, and on abort, mode switch, reload, suspend, `ENABLED=0` and device
  removal, ALL keys still held by the macro are released (through `releaseAll`). A press-only macro (for example `+ctrl`
  or `+shift`) is therefore valid, and there is no "never released" warning.
- Dialog: "Record" start and stop buttons; check box "also record the times between the keys": on, the pauses come from the
  measured gaps; off, the default pause of 30 ms is put between the steps. After recording, every step can be edited,
  deleted and reordered (the key fields use the widgets of decision 5). Recording happens only inside the dialog while
  it has focus; never a global key logger, nothing is recorded or kept outside the dialog.
- Limit: 256 steps per macro (down, up and pause each count); the queue of a running macro is the same 256 steps.

### Stored syntax

Extends the action text of `parseChannelAction` and `parseButtonAction`: `macro:` followed by steps separated by a space
(the space is not allowed inside a step; a space inside an action is an error today, so old files are unaffected and old
daemons reject the line with the existing warning).

| Step | Meaning |
|---|---|
| `+<chord>` | key down (a chord presses its modifiers and key, for example `+ctrl+c`), stays held |
| `-<chord>` | key up (releases it) |
| `wait:<ms>` | pause, 1 to 60000 ms |

A key name is from the closed list in `src/keys.hpp`, as everywhere. Example (select all, copy, then type "ab" with shift
held, 30 ms between steps, and a deliberately press-only tail):

    # [app:editor]
    BUTTON_5 = macro:+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b

At the end of this macro `shift` is still down in the step list and is released by the end-of-macro release. As an
action on a motion the same text is used with a per-side form `macro:<neg-steps>,<pos-steps>` (a side may be `none`).
Steps with tier B keys give the plain load warning; there is no gate.

## Limits

Decided 2026-10-28: the arbitrary limits are raised generously (plenty of memory); a high ceiling stays only against
hostile or broken files. Rate and output limits stay as they are.

| Limit | Before | New | Stage |
|---|---|---|---|
| Profile file size (`kMaxProfileFileBytes`, `src/profile_files.*`) | 64 KiB | 1 MiB | 7a |
| Action text | 256 bytes | 32 KiB | 7a |
| Own functions (`FUNC_*`) per section | 32 | 1000 | 7a (constant), 7e (table) |
| Keys per section | 512 | 10 000 | 7a |
| Macro steps per macro, and macro queue | queue of 16 steps | 256 steps | 7b |
| Steps per side of `step:` / `keys:` (key sequences) | 8 | not needed (macro replaces it) | 7b |
| Modifiers per step | 4 | 4 (the four modifiers exist once) | no change |

Unchanged (rate and output): analog key repeat hard cap `min(100, tick_hz/2)`; at most 4 presses per tick and function
and 8 per tick in total (stage 3); the uinput buffer of 4096 events; a hold longer than 60 s is logged once, not released.

The loader's file size limit (`kMaxProfileFileBytes` in `src/profile_files.*`) and the constants in `config.cpp` must be
changed together and tested in the same stage: a test writes a file just below and just above each limit and checks
that the loader and `config.cpp` agree (a 1 MiB file with 10 000 keys is loaded; a file of 1 MiB plus one byte, an action
of 32 KiB plus one byte, the 10 001st key and the 257th macro step are rejected with a message naming the file or line).

## Stages (small PRs, each with tests)

1. Golden master: `test_mode_runner` and `test_scenarios` record the outputs of all existing modes and profiles (V/H/
   twist, parts, `keys:`/`step:`). No code change.
2. Lift: `LIFT_ACTION`, `LIFT_*`, profile layer, runner slot, UI editable, lift in the safety net.
3. Key engine: `KeyRepeater` / `DigitalTrigger`, rate min/max, `none` per side, tier A/B rule, tick cap, UI check box and
   fields; scenario tests (rate grows with deflection, first press, digital exactly one press per crossing, re-arm, no
   double fire on push/tilt crosstalk), `test_stuck_keys` for all release paths. Constants: the press caps of
   "Limits" are set here.
4. Mouse and hold: `click`, `hold:` with the union of modifiers (zoom plus hold), release tests.
5. Pointer: `Output::moveMouse` is merged (#236); this stage adds the actions `pointer_x/y` and `MOUSE_SPEED` on all
   four systems; fake output test for sums and signs.
6. Presets, docs, `site/`, `po/`, help texts.
7. Own functions: 7a dialog and inline composition (key fields with modifier check boxes and drop-down or capture); constants
   raised here: profile file 1 MiB (`kMaxProfileFileBytes` and `config.cpp` together), action text 32 KiB, keys per
   section 10 000, own functions per section 1000; 7b `macro:` syntax, parser, macro engine (digital only), recording
   dialog; constant: 256 steps per macro and queue; 7c load check: `check_config` reports unused and duplicate functions,
   `security.md` (tier B rule), no gate and no flag; 7d take over from mode (with the option "also take over the tuning")
   and save as mode (after the take-over branch #221 is merged); 7e optional `FUNC_` table. The refactor of
   `tickCustom` into six slots stays strictly behind the golden-master test and apart from new functions.

What only a human with a device can verify: feel and thresholds of analog / digital, lift misfires, crosstalk on tilt with
digital keys, pointer speed and acceleration on all four backends, drag with hold (macOS), tab and window switching per
system, volume and media keys, Wayland (uinput `REL_X/REL_Y`), macros in real programs (timing, recording, release at the end).

## Decisions (recommendation taken; 2026-10-28 additions marked)

1. Reject analog keys with tier B keys in the parser, digital with warning (decided).
2. Threshold as a rate like `STEP_TRIGGER` (yes), the UI shows "about N % of full deflection".
3. One algorithm with defaults equal to today (yes).
4. `TWIST_GUARD` default off (yes).
5. Tap cap 4 per tick (yes, release notes).
6. Pointer on all systems, `Output::moveMouse` merged (decided 2026-10-28); Windows and macOS are part of the release.
7. Lift default `none`, high deadzone, never an analog key (yes).
8. `tickCustom` refactor strictly behind the golden master (yes).
9. Window and tab switching only as presets per system, no built-in defaults (yes).
10. Own functions: inline first, table only if needed (yes); no `ALLOW_RISKY_KEYS` gate (decided 2026-10-28); sequences
    are the digital `macro:` function (decided 2026-10-28); take over copies the action (not a link), the tuning only on
    request (decided 2026-10-28).
