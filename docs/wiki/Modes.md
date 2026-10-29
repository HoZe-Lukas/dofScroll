<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Joystick modes

A joystick mode decides what the motions of the cap (the three channels, push and tilt separately, and the lift) and the device buttons do. Joystick modes are plain text files, no recompiling needed. Full reference: [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md). Parser: [src/modes.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/modes.cpp); key names: [src/keys.hpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/keys.hpp).

Exactly one joystick mode is active: the one named by `MODE` in the resolved profile, otherwise `normal`. Select it in `[global]`, `[device:*]` or `[app:*]` (see [Profiles](Profiles)), from the tray "Joystick mode" menu (switches the running daemon until the next reload; it does not write `MODE`), or with a button action.

## The six motions

A SpaceMouse cap moves in six ways. Every joystick mode says what each of them does:

```
 push forward/back  z  <->  tilt forward/back  rx     (vertical channel)
 push sideways      x  <->  tilt sideways      rz     (horizontal channel)
 twist              ry                                 (zoom channel)
 lift / press       y                                  (in no default mix)
```

| Motion | Axis | Direction | File key (split) | File key (whole channel) |
|---|---|---|---|---|
| 1. Vertical push | `z` | push forward / back | `V_PUSH_ACTION` | `V_ACTION` |
| 2. Vertical tilt | `rx` | tilt forward / back | `V_TILT_ACTION` | `V_ACTION` |
| 3. Horizontal push | `x` | push sideways | `H_PUSH_ACTION` | `H_ACTION` |
| 4. Horizontal tilt | `rz` | tilt sideways | `H_TILT_ACTION` | `H_ACTION` |
| 5. Twist | `ry` | twist the cap | `TWIST_ACTION` | `TWIST_ACTION` |
| 6. Lift / press | `y` | pull up / push down | none (only through a `y` term in a channel mix) | none |

- A key that is not split gives push and tilt of its channel the same action: `V_ACTION` covers motions 1 and 2, `H_ACTION` motions 3 and 4. Set a split key to give push and tilt different jobs (see [Split push and tilt](#split-push-and-tilt) below).
- `default` is the channel's normal behavior: vertical = scroll, horizontal = scroll sideways, twist = zoom (the zoom key plus the wheel: Ctrl unless `ZOOM_MODIFIER` says otherwise, for example `super` on macOS).
- Lift / press is not used by any built-in joystick mode.
- A joystick mode that sets none of these keys behaves like `normal`: every motion does its default.

[Built-in joystick modes](Built-in-modes) has a table with the six motions of every built-in joystick mode.

## Built-in joystick modes

Fourteen joystick modes (`normal`, `off`, `timeline`, `slides`, `media`, `map`, `precision`, `tabs`, `browse`, `sheet`, `photo`, `zoom-pan`, `vertical-only`, `fast`) and five button modes (`latch-nav`, `next-mode`, `prev-mode`, `cycle-favorites`, `reset-zoom`) are built into the program on every system. Nothing has to be copied into your folder to use them. Each one, with its channel actions, overrides and use, is described on [Built-in joystick modes](Built-in-modes). To propose or send a new one, see [Joystick mode requests and contributions](Contributing-modes).

A file in the joystick modes folder with the id of a built-in joystick mode replaces it.

## Writing a joystick mode file

A step-by-step tutorial with copy-paste recipes (horizontal scroll, page keys, tabs, zoom, button modes, testing) is on [How to write a joystick mode](Writing-modes). This section is the short reference.

For your own joystick modes put `<id>.conf` into `~/.config/dofscroll/modes/`. The id is the file name without `.conf` (`[a-z0-9_-]{1,32}`). Format: `KEY = value`, no sections, `#` and `;` comments.

```
NAME = Timeline (scrub / single frames)
DESCRIPTION = Sideways = scrub
V_ACTION = none
H_ACTION = keys:left,right
TWIST_ACTION = keys:left,right
BUTTON_1 = key:space
BUTTON_2 = key:home
SCROLL_H_SENS = 4
```

Any key from the [Configuration reference](Configuration-reference) is a settings override, applied after the device profile and before the app profile. `MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not allowed in a joystick mode file and are rejected with a warning. Limits: 64 joystick mode files, 64 KiB each, 128 overrides per file, regular files only.

The daemon reloads the directory when it changes (about a second), or run `systemctl --user kill -s HUP dofscroll.service`. Check `journalctl --user -u dofscroll.service` for warnings about skipped lines or files.

## Channel actions

`V_ACTION` (channel `SCROLL_V`), `H_ACTION` (`SCROLL_H`), `TWIST_ACTION` (`ZOOM`) and `LIFT_ACTION` (the lift, default `none`, no analog `keys:`):

| Value | Effect |
|---|---|
| `default` | The channel's normal behavior (twist: zoom with dominance detection) |
| `none` | Channel off |
| `wheel_v`, `wheel_h` | Vertical or horizontal mouse wheel |
| `zoom` | Hold the zoom key (`ZOOM_MODIFIER`, default Ctrl) and send the mouse wheel |
| `keys:<neg>,<pos>` | Analog key presses for negative and positive deflection; the rate follows the deflection. Each key can be a chord such as `ctrl+pageup` (up to four modifiers, then one key) or `none`. Only keys that are safe to repeat: a letter, digit, Enter, Delete, Esc, Alt or Super chord is refused ("this key can only be used digitally, use step:") |
| `step:<neg>,<pos>` | One key tap per push (no repeat while held); fires at `STEP_TRIGGER` (steps per second after sensitivity, default 20) and re-arms at half of it. Same keys and chords as `keys:` (any key is allowed, with the load warning); a side can also be `click`, `rightclick` or `middleclick` |
| `hold:<neg>,<pos>` | A key, chord (modifiers alone are fine) or mouse button is held while the motion is deflected beyond the trigger and let go at half of it (digital) |

## The key engine: analog and digital

Every motion (vertical and horizontal push and tilt, twist, lift; both directions) can press a key or chord per direction, in a joystick mode and in a profile. `keys:` is **analog**, `step:` is **digital**; the interface has the check box **Analog**, which only switches the prefix. Three keys per motion tune it, with the prefix `SCROLL_V_PUSH_`, `SCROLL_V_TILT_`, `SCROLL_H_PUSH_`, `SCROLL_H_TILT_`, `ZOOM_` or `LIFT_`:

| Key | Meaning |
|---|---|
| `<prefix>KEY_RATE_MIN` | Analog: lowest repeat rate in presses per second while deflected (0 to 1000, default 0 = none) |
| `<prefix>KEY_RATE_MAX` | Analog: highest repeat rate (default 0 = no limit; never above `min(100, TICK_HZ / 2)`) |
| `<prefix>TRIGGER` | Level that gives one press (digital) or the first press (analog); default `STEP_TRIGGER` |

At most 4 key presses per tick and function and 8 per tick in total are sent (older versions up to 200). Mouse clicks and holds are digital only and release on a joystick mode change, a reload, suspend, `ENABLED = 0`, an unplugged device and the safety net. Feel and thresholds are not verified on a real device. Full text: [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#the-key-engine-analog-and-digital).

## Split push and tilt

The vertical channel mixes push forward/back (`z`) and tilt forward/back (`rx`), the horizontal one push sideways (`x`) and tilt sideways (`rz`). `V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION` and `H_TILT_ACTION` (same values as `V_ACTION`) give the two movements of a channel different functions:

    V_PUSH_ACTION = wheel_v
    V_TILT_ACTION = step:pageup,pagedown
    SCROLL_V_TILT_SENS = 8

- The push part is the translation terms of the channel's mix (`x`, `y`, `z`), the tilt part the rotation terms (`rx`, `rz`), with the weights and signs you configured (defaults: V push `z`, V tilt `rx`, H push `x`, H tilt `-rz`). The twist term `ry` belongs to neither.
- A joystick mode without these keys behaves exactly as before. If a joystick mode sets at least one split key of a channel, that channel runs as two independent sub-channels; a part without its own key uses `V_ACTION` / `H_ACTION`.
- Each part uses the sensitivity, dead zone, curve, smoothing and sign of its channel. `SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>` replace single values with absolute values (not multipliers).

Details: [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#split-push-and-tilt).

## Button actions

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. A joystick mode may bind keys or clicks that only make sense in that joystick mode; they are active only while the joystick mode is active, which the driver cannot do. Examples: a button that always sends Esc belongs in the driver. A button that sends Space (play/pause) only while the `timeline` joystick mode is active, a button with `mode:next` and a button with `nav` belong in dofscroll. See [Buttons](Buttons).

`BUTTON_<n> = <action>`, buttons numbered **from 1**, in joystick mode files and the main config alike. `BUTTON_0` is rejected. At most 32, and `n` must not exceed the factory button count of the model, see [Buttons](Buttons).

| Action | Effect |
|---|---|
| `none` | Nothing |
| `click`, `rightclick`, `middleclick` | Mouse button, held while the device button is held |
| `nav` | While held, X/Y send arrow keys instead of scrolling |
| `key:<name>` | Holds the key while the button is held |
| `key:<mod>+<key>` | A chord such as `key:ctrl+0`, tapped once per press (same grammar as the chords in `keys:`) |
| `mode:<id>` | Switches to joystick mode `<id>` on press |
| `mode:next` | Switches to the next loaded joystick mode in alphabetical order of id (built-ins included), wrapping around |
| `toggle:nav` | Every press switches the navigation mode on or off (latched `nav`) |
| `hold:mode:<id>` | Joystick mode `<id>` is effective while the button is held; on release the previous state returns (e.g. `BUTTON_3 = hold:mode:precision`) |
| `mode:next:<id>,<id>,...` / `mode:prev:<id>,...` | The same cycle through exactly the listed joystick modes, in that order (wrapping; unknown ids skipped). Written by a button mode with a `CYCLE` list, see the Buttons page |
| `mode:prev` | The mirror of `mode:next`: the previous joystick mode in alphabetical order, wrapping around |
| `toggle:mode:<id>` | First press switches to joystick mode `<id>`, the next press goes back to the profile's joystick mode |
| `buttonmode:<id>` | Behaves like the action of `buttons/<id>.conf` (a button mode); unknown ids do nothing |

Toggle state is shown in the tray ("Navigation mode on") and cleared by a reload (also the tray's "Release all keys"), a restart, suspend or `ENABLED = 0`, and any joystick mode change. A joystick mode binding wins over a `BUTTON_<n>` binding of `[global]` and `[device:*]`; a `BUTTON_<n>` of an `[app:*]` profile wins over the joystick mode. A switch by `mode:` lasts until the daemon restarts, the config or joystick modes reload, or another switch happens; held keys and buttons are released on every switch. An unknown joystick mode id in a joystick mode file is dropped with a warning.

Key names are lower case: `ctrl shift alt super`, `up down left right home end pageup pagedown insert delete`, `space enter esc tab backspace`, `f1`-`f24` (macOS: up to `f20`), `a`-`z`, `0`-`9`, `plus minus`, the keypad (`kp_0`-`kp_9 kp_enter kp_plus kp_minus kp_multiply kp_divide kp_decimal`), punctuation (`comma period slash backslash semicolon apostrophe bracket_left bracket_right grave equal`), `caps_lock print scroll_lock pause menu num_lock` and the media keys (`play_pause media_next media_prev media_stop volume_up volume_down volume_mute`). macOS cannot send `f21`-`f24`, `print`, `scroll_lock`, `pause`, `num_lock` or the media keys.

## Loader warnings

The loader skips what it cannot use and logs a warning: invalid lines or files, keys that are not allowed in a joystick mode file, `BUTTON_0` or a button above the limit, unknown `mode:` targets, and modifier-capable bindings (see below). Warnings are in the daemon journal.

## Safety: joystick mode files can press any key

A joystick mode file is code-like. While a button is held or an axis is deflected it can press any key from the key list, including Ctrl, Alt, Super, function keys, Delete and Enter. Chords are reachable: Ctrl is held while a channel zooms, so a `keys:` action on another channel can send Ctrl+W or Ctrl+Q, and `keys:` accepts chords itself (`ctrl+pageup`); the daemon warns about chords with `alt`, `super` or non-navigation keys. It cannot type arbitrary text or send codes outside the key list, and every effect needs the physical device to be moved or pressed.

Read every file you get from somebody else before you put it into `~/.config/dofscroll/modes/` (the built-in joystick modes need no copying). Look for `key:`, `keys:`, `zoom` and `mode:`. The daemon warns at load time about modifier-capable bindings, but that does not replace reading. If you share a joystick mode, say which keys it presses. See also [Security](Security) and [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#safety-and-sharing-modes).

A joystick mode that turns output off (like `off`) leaves the device inactive. Get out with the tray "Joystick mode" menu, a `mode:next` or `mode:<id>` binding in the main config, or by editing `MODE` and reloading.

## Joystick mode library

In the interface (page Joystick modes) you can export a joystick mode file, rename it (new name and id; profiles that
use the old id are listed first and keep pointing at it), see which profiles use a joystick mode, and import
`.conf` files by dropping them onto the page. Imports get the same checks and the safety
confirmation as the Import button. The id `release` is a normal joystick mode id.

## Editing

The tray "Joystick mode" submenu only switches the joystick mode; the tray menu also has "Open joystick modes folder". Editing, "Edit mode file" and "Create editable copy" (for built-ins without a file; it never overwrites) are in the interface: the Editor and Joystick modes pages, and the General tab of a profile.
