<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Modes

A mode decides what the three channels and the device buttons do. Modes are plain files, no
recompiling needed. Parser: `src/modes.hpp`, `src/modes.cpp`. Key names: `src/keys.hpp`.

Exactly one mode is active. It is the one named by `MODE` in the resolved profile, otherwise
`normal`. Select it in `[global]`, `[device:*]` or `[app:*]` (see
[configuration.md](configuration.md)). The tray also has a quick "Mode" menu that sets the global
`MODE`.

## Mode files

Location: `~/.config/dofscroll/modes/<id>.conf`. The file name without `.conf` is the mode id
(`[a-z0-9_-]{1,32}`). A file with the id of a built-in mode replaces the built-in one. The daemon
reloads the directory when its contents change.

Format: `KEY = value`, no sections, `#` and `;` comments.

    NAME = Timeline (scrub / single frames)    # display name, optional
    DESCRIPTION = Sideways = scrub              # optional
    V_ACTION = none
    H_ACTION = keys:left,right
    TWIST_ACTION = keys:left,right
    BUTTON_1 = key:space
    BUTTON_2 = key:home
    SCROLL_H_SENS = 4                           # any profile key is an override

`MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not allowed inside a mode file (the daemon rejects
them with a warning). Any other key from [configuration.md](configuration.md) is a settings override. The overrides are applied after the device profile and before the app
profile.

Limits: up to 64 mode files, each up to 64 KiB, up to 128 overrides per file, regular files only.
Invalid lines or files are skipped with a warning in the log.

## Built-in modes

| Mode | Behavior |
|---|---|
| `normal` | All channels `default`, no bindings. Scroll, Ctrl+scroll zoom, `NAV_BUTTON` and `CLICK_BUTTON`. |
| `off` | All channels `none`. Switches control off without `ENABLED = 0`. |
| `timeline` | Vertical `none`, horizontal and twist `keys:left,right`. Scrubbing and single frames for video editing. |

The first start also seeds three example files into `~/.config/dofscroll/modes/`:
`browse-keys` (page up/down, left/right, zoom, Enter and Esc on buttons 1 and 2), `cycle-modes`
(button 2 = `mode:next`) and `timeline` (adds Space on button 1 and Home on button 2).

## Channel actions

For `V_ACTION` (channel `SCROLL_V`), `H_ACTION` (`SCROLL_H`) and `TWIST_ACTION` (`ZOOM`):

| Value | Effect |
|---|---|
| `default` | The channel's normal behavior: vertical wheel, horizontal wheel, twist = zoom with dominance detection. |
| `none` | Channel off. |
| `wheel_v`, `wheel_h` | Vertical or horizontal mouse wheel. |
| `zoom` | Hold Ctrl and send the mouse wheel. |
| `keys:<neg>,<pos>` | Key presses: `<neg>` for negative, `<pos>` for positive deflection. The rate follows the deflection. Dead zone, curve, smoothing and sensitivity of the channel apply. |

## Button actions

`BUTTON_<n> = <action>`. Buttons are numbered from 1 (`BUTTON_1` is the first button), in mode files
and in the main config alike. `BUTTON_0` is rejected with "buttons are numbered from 1". At most 32.
`n` must not exceed the factory button count of the device model (Pro 15, Compact 2, Enterprise 31;
table in `src/device_db.cpp`). The count is not the one `spacenavd` reports.

| Action | Effect |
|---|---|
| `none` | Nothing. |
| `click`, `rightclick`, `middleclick` | Mouse button, held while the device button is held (drag works). |
| `nav` | While held, X/Y send arrow keys instead of scrolling. |
| `key:<name>` | Holds the key while the button is held. |
| `mode:<id>` | Switches to mode `<id>` when the button is pressed. |
| `mode:next` | Switches to the next loaded mode in alphabetical order of id, built-ins included, wrapping around. |

The tray's "Mode" submenu uses the same runtime switch and never writes `MODE` into `[global]`:
it writes the mode id into `dofscroll-mode-request` in the runtime directory (user-only, one line,
valid id only) and asks the daemon to reload; the daemon applies it after re-reading config and
modes, then deletes the file. Like a button switch it lasts until the next reload, a restart or
another switch (so toggling "Enabled" or saving the config drops it).

A binding in the active mode for a button wins over a `BUTTON_<n>` binding in the main config for
the same button.

A switch by `mode:` stays until the daemon restarts, the config or modes directory reloads, or
another switch happens. Held keys and buttons are released on every switch. An unknown mode id in
a mode file is dropped with a warning.

A mode whose three channel actions are all `default` runs the normal path, so dominance-based zoom
and `NAV_BUTTON`/`CLICK_BUTTON` still work, and its button bindings apply in addition.

## Key names

Names are lower case.

| Group | Names |
|---|---|
| Modifiers | `ctrl`, `shift`, `alt`, `super` |
| Arrows and navigation | `up`, `down`, `left`, `right`, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete` |
| Editing | `space`, `enter`, `esc`, `tab`, `backspace` |
| Function keys | `f1` to `f12` |
| Letters and digits | `a` to `z`, `0` to `9` |
| Plus and minus | `plus`, `minus` (main keyboard), `kp_plus`, `kp_minus` (keypad) |

`plus` is the main-keyboard plus. On X11 it is resolved from the keyboard layout. On uinput it
is sent as Shift and `=` (a US-style layout is assumed). `kp_plus` and `kp_minus` are the keypad keys and
do not depend on the layout. Programs differ in which of them they accept for zoom.

## Example: timeline mode

For video editing (for example in DaVinci Resolve, whose behavior is not verified here):

    [app:resolve]
    MODE = timeline
    SCROLL_H_SENS = 4     # scrubbing
    ZOOM_SENS = 1         # single frames

The timeline must have keyboard focus. Reverse the twist with `ZOOM_SIGN = -1`.

## Safety and sharing modes

A mode file is code-like. While a device button is held or an axis is deflected, it can press
any key on the list in [Key names](#key-names), including modifiers (Ctrl, Alt, Super), function
keys, Delete and Enter. Chords are reachable: Ctrl is held while a channel zooms, so a `keys:`
action on another channel can send Ctrl+W or Ctrl+Q, and `key:ctrl` plus `key:alt` on two buttons
combine with a third key. A mode file cannot type arbitrary text or send codes outside the key list,
and every effect needs the physical device to be moved or pressed.

Only use mode files from sources you trust, and read a file before you copy it into
`~/.config/dofscroll/modes/`. When you review one, look for:

- `key:` bindings, especially `ctrl`, `alt`, `super`, `shift`, `delete`, `esc`, `enter` and `f1` to `f12`.
- `keys:<negative>,<positive>` actions on `V_ACTION`, `H_ACTION` or `TWIST_ACTION`, especially
  letters and digits, or in combination with a `zoom` channel (Ctrl is held then).
- `zoom` on any channel, because it holds Ctrl.
- `mode:` bindings that switch to another mode, and the other files such a mode points to.
- Settings overrides that change sensitivities, signs or button behavior.

The daemon warns at load time (in the log) about modifier-capable bindings, but the warning does not
replace reading the file. If you share a mode, say what keys it presses.

A mode that turns output off or ignores the device (for example the built-in `off`) leaves the
device inactive. To get out of it, use the tray menu "Mode", a `mode:next` or `mode:<id>` binding
(put it in the main config so it stays available in every mode, or in the mode itself), or edit
`MODE` in the config and reload.

## Add your own mode

1. Create `~/.config/dofscroll/modes/<id>.conf` with the keys above.
2. Wait about a second (automatic reload), or run `systemctl --user kill -s HUP dofscroll.service`.
3. Select it with `MODE = <id>` in a profile, from the tray "Mode" menu, or with a `mode:<id>`
   button action.
4. Check `journalctl --user -u dofscroll.service` for warnings about skipped lines.

## Editing modes from the tray

The tray "Mode" submenu lists, below the mode choices, one entry per mode:

- `Edit mode file: <name>` opens the mode's file in the default editor.
- `Create editable copy of <name>...` appears for built-in modes that have no file yet (normal,
  off, timeline). It writes a commented template `<id>.conf` with the built-in's name, description
  and channel actions into the modes folder (mode 0600, never overwriting an existing file) and
  opens it. A file of the same name replaces the built-in mode.
- `Open modes folder` opens the folder in the file manager.

The editor is started outside the tray's sandbox, like "Open config file". After saving, the
daemon reloads the modes by itself and the new entry appears in the menu within about a second.

The example modes (`timeline`, `browse-keys`, `cycle-modes`) are good starting points: copy one and
change it.
