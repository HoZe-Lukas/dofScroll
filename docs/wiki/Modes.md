<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Modes

A mode decides what the three channels and the device buttons do. Modes are plain text files, no recompiling needed. Full reference: [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md). Parser: [src/modes.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/modes.cpp); key names: [src/keys.hpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/keys.hpp).

Exactly one mode is active: the one named by `MODE` in the resolved profile, otherwise `normal`. Select it in `[global]`, `[device:*]` or `[app:*]` (see [Profiles](Profiles)), from the tray "Mode" menu (sets the global `MODE`), or with a button action.

## Built-in modes

| Mode | Behavior |
|---|---|
| `normal` | All channels `default`, no bindings: scroll, Ctrl+scroll zoom, `NAV_BUTTON` (the older `CLICK_BUTTON` also still works) |
| `off` | All channels `none`; switches control off without `ENABLED = 0` |
| `timeline` | Vertical `none`, horizontal and twist `keys:left,right` |

A file in the modes folder with the id of a built-in mode replaces it.

## Writing a mode file

Put `<id>.conf` into `~/.config/dofscroll/modes/`. The id is the file name without `.conf` (`[a-z0-9_-]{1,32}`). Format: `KEY = value`, no sections, `#` and `;` comments.

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

Any key from the [Configuration reference](Configuration-reference) is a settings override, applied after the device profile and before the app profile. `MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not allowed in a mode file and are rejected with a warning. Limits: 64 mode files, 64 KiB each, 128 overrides per file, regular files only.

The daemon reloads the directory when it changes (about a second), or run `systemctl --user kill -s HUP dofscroll.service`. Check `journalctl --user -u dofscroll.service` for warnings about skipped lines or files.

## Channel actions

`V_ACTION` (channel `SCROLL_V`), `H_ACTION` (`SCROLL_H`) and `TWIST_ACTION` (`ZOOM`):

| Value | Effect |
|---|---|
| `default` | The channel's normal behavior (twist: zoom with dominance detection) |
| `none` | Channel off |
| `wheel_v`, `wheel_h` | Vertical or horizontal mouse wheel |
| `zoom` | Hold Ctrl and send the mouse wheel |
| `keys:<neg>,<pos>` | Key presses for negative and positive deflection; the rate follows the deflection. Each key can be a chord such as `ctrl+pageup` (up to four modifiers, then one key) |
| `step:<neg>,<pos>` | One key tap per push (no repeat while held); fires at `STEP_TRIGGER` (steps per second after sensitivity, default 20) and re-arms at half of it. Same keys and chords as `keys:` |

## Button actions

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. A mode may bind keys or clicks that only make sense in that mode; they are active only while the mode is active, which the driver cannot do. Examples: a button that always sends Esc belongs in the driver. A button that sends Space (play/pause) only while the `timeline` mode is active, a button with `mode:next` and a button with `nav` belong in dofscroll. See [Buttons](Buttons).

`BUTTON_<n> = <action>`, buttons numbered **from 1**, in mode files and the main config alike. `BUTTON_0` is rejected. At most 32, and `n` must not exceed the factory button count of the model, see [Buttons](Buttons).

| Action | Effect |
|---|---|
| `none` | Nothing |
| `click`, `rightclick`, `middleclick` | Mouse button, held while the device button is held |
| `nav` | While held, X/Y send arrow keys instead of scrolling |
| `key:<name>` | Holds the key while the button is held |
| `mode:<id>` | Switches to mode `<id>` on press |
| `mode:next` | Switches to the next loaded mode in alphabetical order of id (built-ins included), wrapping around |
| `toggle:nav` | Every press switches the navigation mode on or off (latched `nav`) |
| `toggle:mode:<id>` | First press switches to mode `<id>`, the next press goes back to the profile's mode |

Toggle state is shown in the tray ("Navigation mode on") and cleared by a reload (also the tray's "Release all keys"), a restart, suspend or `ENABLED = 0`, and any mode change. A mode binding wins over a `BUTTON_<n>` binding in the main config. A switch by `mode:` lasts until the daemon restarts, the config or modes reload, or another switch happens; held keys and buttons are released on every switch. An unknown mode id in a mode file is dropped with a warning.

Key names are lower case: `ctrl shift alt super`, `up down left right home end pageup pagedown insert delete`, `space enter esc tab backspace`, `f1`-`f12`, `a`-`z`, `0`-`9`, `plus minus kp_plus kp_minus`.

## Loader warnings

The loader skips what it cannot use and logs a warning: invalid lines or files, keys that are not allowed in a mode file, `BUTTON_0` or a button above the limit, unknown `mode:` targets, and modifier-capable bindings (see below). Warnings are in the daemon journal.

## Safety: mode files can press any key

A mode file is code-like. While a button is held or an axis is deflected it can press any key from the key list, including Ctrl, Alt, Super, function keys, Delete and Enter. Chords are reachable: Ctrl is held while a channel zooms, so a `keys:` action on another channel can send Ctrl+W or Ctrl+Q, and `keys:` accepts chords itself (`ctrl+pageup`); the daemon warns about chords with `alt`, `super` or non-navigation keys. It cannot type arbitrary text or send codes outside the key list, and every effect needs the physical device to be moved or pressed.

Read every file before you copy it into `~/.config/dofscroll/modes/`. Look for `key:`, `keys:`, `zoom` and `mode:`. The daemon warns at load time about modifier-capable bindings, but that does not replace reading. If you share a mode, say which keys it presses. See also [Security](Security) and [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#safety-and-sharing-modes).

A mode that turns output off (like `off`) leaves the device inactive. Get out with the tray "Mode" menu, a `mode:next` or `mode:<id>` binding in the main config, or by editing `MODE` and reloading.

## Example modes

Shipped in [examples/modes/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/modes) and seeded on first start:

| File | Behavior |
|---|---|
| `browse-keys.conf` | Up/down = page up/down, sideways = left/right, twist = zoom |
| `cycle-modes.conf` | Default channels; button 2 = `mode:next` |
| `slides.conf` | Up/down = page up/down, sideways and twist off; `SCROLL_V_SENS = 2` |
| `media.conf` | Up/down = arrow up/down (volume), sideways = left/right (seek), twist = `j`/`l`; program-specific keys |
| `map.conf` | Arrow keys for up/down and sideways, twist = `minus`/`plus` (layout caveat, see Key names) |
| `precision.conf` | Default channels with low sensitivities (`SCROLL_V_SENS = 2`, `SCROLL_H_SENS = 1`, `ZOOM_SENS = 2`) |
| `tabs.conf` | Sideways = `ctrl+pageup` / `ctrl+pagedown` (previous / next tab); needs no chord warning |
| `timeline.conf` | Sideways and twist = left/right; button 1 Space, button 2 Home (only while this mode is active); `SCROLL_H_SENS = 4` |

Use `timeline` for video editing in a program of your choice, for example `[app:resolve]` with `MODE = timeline` (DaVinci Resolve's behavior is not verified here).

## Editing from the tray

The tray "Mode" submenu has `Edit mode file`, `Create editable copy` (for built-ins without a file, writes a template with mode 0600 and never overwrites) and `Open modes folder`.
