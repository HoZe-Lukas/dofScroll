<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Joystick modes

A joystick mode decides what the motions of the cap (the three channels, push and tilt separately, and the lift) and the device buttons do. Joystick modes are plain files, no
recompiling needed. Parser: `src/modes.hpp`, `src/modes.cpp`. Key names: `src/keys.hpp`.

Exactly one joystick mode is active (per device when several devices arrive separately, see [multi-device.md](multi-device.md)). It is the one named by `MODE` in the resolved profile, otherwise
`normal`. Select it in `[global]`, `[device:*]` or `[app:*]` (see
[configuration.md](configuration.md)). The tray also has a quick "Joystick mode" menu that switches the running daemon
(it does not change `MODE` in the file, see [Button actions](#button-actions)).

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
| 6. Lift / press | `y` | pull up / push down | `LIFT_ACTION` (default `none`) | `LIFT_ACTION` |

- A key that is not split gives push and tilt of its channel the same action: `V_ACTION` covers motions 1 and 2, `H_ACTION` motions 3 and 4. Set a split key to give push and tilt different jobs ([Split push and tilt](#split-push-and-tilt)).
- `default` is the channel's normal behavior: vertical = scroll, horizontal = scroll sideways, twist = zoom (the zoom modifier, by default Ctrl, plus the wheel).
- Lift / press has one key, `LIFT_ACTION`, and no split. It does nothing by default and is not used by any built-in joystick mode ([Lift/press](#liftpress-is-unused)); it takes `none`, `wheel_v`, `wheel_h`, `zoom`, `pointer_x`, `pointer_y` and `step:<neg>,<pos>`, never an analog `keys:`.
- A joystick mode that sets none of these keys behaves like `normal`: every motion does its default.

The [table of the built-in joystick modes](#built-in-joystick-modes) lists what each of the six motions does in each of them.

## Joystick mode files

Location: `~/.config/dofscroll/modes/<id>.conf`. The file name without `.conf` is the joystick mode id
(`[a-z0-9_-]{1,32}`). A file with the id of a built-in joystick mode replaces the built-in one. The daemon
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

`MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not allowed inside a joystick mode file (the daemon rejects
them with a warning). Any other key from [configuration.md](configuration.md) is a settings override. The overrides are applied after the device profile and before the app
profile.

Limits: up to 64 joystick mode files, each up to 64 KiB, up to 128 overrides per file, regular files only. A line holds up
to 33 KiB, so an action (up to 32 KiB, a macro of 256 steps fits easily) fits on one line.
Invalid lines or files are skipped with a warning in the log.

## Built-in joystick modes

Sixteen joystick modes ship inside the program on every system. They need no file, no examples folder
and no install path, and nothing is copied into your configuration directory. They appear
everywhere a joystick mode can be chosen (profile drop-downs, tray menu, Overview, `MODE = <id>`,
`mode:<id>` buttons) with the origin "Built in".

| Joystick mode | Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press | Sensitivity overrides |
|---|---|---|---|---|---|---|---|
| `normal` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | none |
| `off` | Off | Off | Off | Off | Off | Not used | none |
| `timeline` | Off | Off | Keys: Left / Right | Keys: Left / Right | Keys: Left / Right | Not used | none |
| `slides` | Keys: Page Up / Page Down | Keys: Page Up / Page Down | Off | Off | Off | Not used | SCROLL_V_SENS = 2 |
| `media` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Steps: J / L | Not used | none |
| `map` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Minus / Plus | Not used | none |
| `precision` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | SCROLL_V_SENS = 2, SCROLL_H_SENS = 1, ZOOM_SENS = 2 |
| `tabs` | Scroll | Scroll | Keys: Ctrl+Page Up / Ctrl+Page Down | Keys: Ctrl+Page Up / Ctrl+Page Down | Zoom | Not used | none |
| `browse` | Keys: Page Up / Page Down | Keys: Page Up / Page Down | Keys: Left / Right | Keys: Left / Right | Zoom | Not used | none |
| `sheet` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Page Up / Page Down | Not used | none |
| `photo` | Zoom | Zoom | Keys: Left / Right | Keys: Left / Right | Off | Not used | none |
| `zoom-pan` | Zoom | Zoom | Scroll sideways | Scroll sideways | Scroll sideways | Not used | none |
| `vertical-only` | Scroll | Scroll | Off | Off | Off | Not used | none |
| `fast` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | SCROLL_V_SENS = 12, SCROLL_H_SENS = 10, ZOOM_SENS = 12 |
| `volume` | Keys: Volume_up / Volume_down | Keys: Volume_up / Volume_down | Scroll sideways | Scroll sideways | Zoom | Not used | none |
| `tracks` | Scroll | Scroll | Steps: Media_prev / Media_next | Steps: Media_prev / Media_next | Zoom | Not used | none |

In the table, `Scroll` is the vertical mouse wheel, `Scroll sideways` the horizontal one and `Zoom` is Ctrl plus the wheel; `Keys: A / B` is `keys:A,B` in the file. Push and tilt of a channel do the same in every built-in joystick mode, and `normal` sends scroll and zoom plus the buttons you bind in the profile (the deprecated `NAV_BUTTON` and `CLICK_BUTTON` still work). `off` switches control off without `ENABLED = 0`.

Their behavior in real programs is not verified. The built-in joystick modes contain no button-to-key
bindings, see [Buttons and the vendor driver](#buttons-and-the-vendor-driver).

The text of `slides`, `media`, `map`, `precision`, `tabs`, `browse`, `sheet`, `photo`, `zoom-pan`, `vertical-only`, `fast`, `volume` and `tracks` is the single source
`examples/modes/<id>.conf`; the build embeds it into the program, and the program parses it with the
same parser as a user file. `timeline`, `normal` and `off` are defined in code. The embedded set is
never written to disk.

**Changing a built-in.** In the interface, "Create editable copy" writes `~/.config/dofscroll/modes/<id>.conf`
with the same id. A user file with the id of a built-in replaces it until you delete the file (the
built-in comes back). A user file that is byte-identical to a shipped example (for example the copy
that older versions made on first start, in any of the texts they ever shipped: `examples/legacy/modes`
holds several per id) is the built-in itself: the loader ignores it, and the User modes page labels it
"Copy of a shipped example, not used" so you can remove it. A copy made by a very early version, for
example an old `timeline.conf` with button bindings, therefore no longer replaces the built-in `timeline`.
Older versions also copied `browse-keys.conf` (now the built-in `browse`) and `cycle-modes.conf`;
if you still use them, edit your profiles to `MODE = browse` or bind the `next-mode` button mode instead. An
unchanged `browse-keys.conf` or `cycle-modes.conf` is ignored by the loader, so remove the file
if you want it gone; a file you changed keeps working under its own id.

There is no built-in `cycle-modes` mode: it only bound `mode:next` to button 2 in an otherwise default
joystick mode, which the button mode `next-mode` does better and in every joystick mode.

**`mode:next` / `mode:prev` with the built-in library.** They cycle through *all* loaded joystick modes in
alphabetical order of the id, built-ins included: `browse`, `fast`, `map`, `media`, `normal`, `off`,
`photo`, `precision`, `sheet`, `slides`, `tabs`, `timeline`, `tracks`, `vertical-only`, `volume`, `zoom-pan` plus your own, so the cycle is long. Use `mode:<id>` or
`toggle:mode:<id>` for a short fixed set. Built-in joystick modes have no bindings of their own, so a cycle
started from a joystick mode of yours stops at the next built-in unless you give that joystick mode a binding too
(create an editable copy, or switch back through the tray menu).

## Channel actions

For `V_ACTION` (channel `SCROLL_V`), `H_ACTION` (`SCROLL_H`), `TWIST_ACTION` (`ZOOM`) and `LIFT_ACTION` (`LIFT`; its default is `none`, and the analog `keys:` is rejected there, see [Lift/press](#liftpress-is-unused)):

| Value | Effect |
|---|---|
| `default` | The channel's normal behavior: vertical wheel, horizontal wheel, twist = zoom with dominance detection. |
| `none` | Channel off. |
| `wheel_v`, `wheel_h` | Vertical or horizontal mouse wheel. |
| `pointer_x`, `pointer_y` | Move the mouse pointer: `pointer_x` right (positive) and left, `pointer_y` down (positive) and up. Continuous and analog, the further you push the faster it moves, always: there is no digital variant. The speed is the global `MOUSE_SPEED` (pixels per step, default 8, 0.1 to 200); dead zone, curve, smoothing, sensitivity and direction (`*_SIGN`) of the channel apply. At most 127 pixels per tick and axis; the pixel remainder is dropped when the motion returns to rest, on a mode switch, reload, suspend, `ENABLED = 0` and when the device is removed, so the pointer never drifts on. The same function on two motions adds up (push and tilt together), `pointer_x` on one and `pointer_y` on another move diagonally. Valid on every motion, in a profile and in a joystick mode. Nothing is held on the output, so a stuck pointer is not possible. Alt/Super-like chord warnings do not apply. |
| `zoom` | Hold the zoom modifier (`ZOOM_MODIFIER`, default Ctrl) and send the mouse wheel. |
| `keys:<neg>,<pos>` | Analog key presses: `<neg>` for negative, `<pos>` for positive deflection. The rate follows the deflection (limits: [key engine](#the-key-engine-analog-and-digital)). Dead zone, curve, smoothing and sensitivity of the channel apply. Each argument can be a chord or `none`, see below. Only for keys that are safe to repeat (tier A, see below): a letter, digit, Enter, Delete, Esc, Alt or Super chord is refused with "this key can only be used digitally, use step:". |
| `step:<neg>,<pos>` | One key tap per push instead of a rate, with hysteresis (digital); see below. Any key. A side can also be `click`, `rightclick` or `middleclick` (one mouse click per push). |
| `hold:<neg>,<pos>` | A key, a chord or a mouse button is held while the motion is deflected beyond the trigger and let go when it has fallen back to half of it (digital); see [Mouse clicks and holds](#mouse-clicks-and-holds). |
| `macro:<neg>,<pos>` | A list of key down, key up and pause steps that runs once per push, one macro per direction (digital); see [Macros](#macros). |

The same keys (`V_ACTION`, `H_ACTION`, `TWIST_ACTION`, `LIFT_ACTION` and the four split keys) are also allowed **in a profile**
(`[global]`, `[device:*]`, `[app:*]` of `dofscroll.conf`) for one program or device, for example `TWIST_ACTION = none`
in `[app:code]`. A program profile wins over the joystick mode; `[global]` and `[device:*]` sit below it (a joystick mode
that sets the channel keeps it; `[global]` and `[device:*]` are two layers, so a device `V_ACTION` also drops a split set in `[global]`). Details and precedence: [configuration.md](configuration.md#precedence). Likewise
`BUTTON_<n>` of a program profile wins over the binding of the joystick mode for the same button.

## Button actions

`BUTTON_<n> = <action>`. Buttons are numbered from 1 (`BUTTON_1` is the first button), in joystick mode files
and in the main config alike. `BUTTON_0` is rejected with "buttons are numbered from 1". At most 32.
`n` must not exceed the factory button count of the device model (Pro 15, Compact 2, Enterprise 31;
table in `src/device_db.cpp`). The count is not the one `spacenavd` reports.

| Action | Effect |
|---|---|
| `none` | Nothing. |
| `click`, `rightclick`, `middleclick` | Mouse button, held while the device button is held (drag works). |
| `nav` | While held, X/Y send arrow keys instead of scrolling. |
| `key:<name>` | Holds the key while the button is held. |
| `macro:<steps>` | A macro: key down, key up and pause steps that run once when the button is pressed (and optionally a second list when it is released); see [Macros](#macros). |
| `key:<mod>+...+<key>` | A key chord such as `key:ctrl+0` or `key:ctrl+shift+n`: zero to four of `ctrl`, `shift`, `alt`, `super`, then one key that is not a modifier (the same grammar and limits as the chords in `keys:`). Tapped once when the button goes down (modifiers pressed in the order written, then the key, then released in reverse); nothing happens on release, so holding the button does not repeat it and nothing can stay pressed. |
| `mode:<id>` | Switches to joystick mode `<id>` when the button is pressed. |
| `mode:next` | Switches to the next loaded joystick mode in alphabetical order of id, built-ins included, wrapping around. |
| `hold:mode:<id>` | The joystick mode `<id>` is effective while the button is held; on release the state from before the press comes back: a `mode:<id>` / `toggle:mode:<id>` override that was in effect, else the joystick mode the profile gives. Example: `BUTTON_3 = hold:mode:precision` (slow and fine while the button is held). The release is noticed even if the held joystick mode does not bind the button. Any other joystick mode switch during the hold ends it without restoring; a reload, suspend / `ENABLED = 0` / session switch and an unplugged device end it too (the previous state comes back). `next` and `prev` are not valid; an unknown id is dropped with a warning at load. The status file shows `hold:<id>` in the `toggle=` line. |
| `mode:next:<id>,<id>,...` / `mode:prev:<id>,...` | The same cycle through exactly the listed joystick modes, in that order (wrapping; unknown ids skipped). Written by a button mode with a `CYCLE` list (see [button-modes.md](button-modes.md#choosing-the-joystick-modes-to-cycle-through)); this is how you choose which joystick modes the button reaches and in which order. |
| `mode:prev` | The mirror of `mode:next`: switches to the previous id in alphabetical order, wrapping from the first to the last. Stateless (no history). `next` and `prev` are reserved targets: a joystick mode file called `next.conf` or `prev.conf` still loads, but `mode:next` and `mode:prev` always mean the cycle action. Not allowed in `toggle:mode:<id>`. |
| `toggle:nav` | Every press switches the navigation mode on or off (arrow keys instead of scrolling), like a held `nav` button but latched. A held `nav` button and the latch add up. |
| `toggle:mode:<id>` | The first press switches to joystick mode `<id>`, the next press goes back to the joystick mode the profile gives. `next` is not allowed. |
| `buttonmode:<id>` | Behaves like the `ACTION` of the button mode file `buttons/<id>.conf`, see [button-modes.md](button-modes.md). Not nestable. An unknown id does nothing. |

Toggles are state that only exists while the daemon runs. The status file shows it in the `toggle=` line
(`nav`, `mode:<id>`, or both) and the tray shows "Navigation mode on" next to the joystick mode. The state is cleared by
a reload (saving the config, the tray's "Release all keys", a `SIGHUP`), by a restart, when the daemon
becomes inactive (suspend, `ENABLED = 0`, session switch) and by any joystick mode change, which releases everything held.
`toggle:mode:<id>` is a runtime switch like `mode:<id>`: it stays across profile changes until it is toggled off.
`BUTTON_<n>` in the main config works for all of these, so a toggle bound there also works in the joystick mode it
switched to; a binding in a joystick mode file for the same button wins. Plain `key:` or `click` toggles do not exist on
purpose: a key that is switched on and off everywhere belongs in the vendor driver.

The tray's "Joystick mode" submenu uses the same runtime switch and never writes `MODE` into `[global]`:
it writes the joystick mode id into `dofscroll-mode-request` in the runtime directory (user-only, one line,
valid id only) and asks the daemon to reload; the daemon applies it after re-reading config and
joystick modes, then deletes the file. Like a button switch it lasts until the next reload, a restart or
another switch (so toggling "Enabled" or saving the config drops it).

The request file is treated as untrusted input: the writer never follows a link (it removes
whatever is at the name and creates the file exclusively, user-only); the daemon opens it without
following links, requires the current user as owner and no group/other permissions (POSIX), reads at
most 64 bytes, accepts only a valid joystick mode id (`[a-z0-9_-]{1,32}`) that is a loaded joystick mode, ignores a
request older than 30 seconds (a reload that never arrived) and removes the file in every case.

The id `release` is an ordinary joystick mode id: a file `release.conf` is valid and `BUTTON_n = mode:release`
switches to it. (The daemon's internal notice that a `hold:mode:` button was released is a request
string with a colon, which can never be a joystick mode id.)

A binding in the active joystick mode for a button wins over a `BUTTON_<n>` binding of `[global]` and `[device:*]` for
the same button; a `BUTTON_<n>` of an `[app:*]` profile wins over the joystick mode's binding.

A switch by `mode:` stays until the daemon restarts, the config or joystick modes directory reloads, or
another switch happens. Held keys and buttons are released on every switch. An unknown joystick mode id in
a joystick mode file is dropped with a warning.

A joystick mode whose three channel actions are all `default` runs the normal path, so dominance-based zoom
and the deprecated `NAV_BUTTON`/`CLICK_BUTTON` still work, and its button bindings apply in addition.

## Buttons and the vendor driver

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles.

| Where | What |
|---|---|
| Driver | A button mapping that is the same everywhere and always: Esc, Enter, a click, a shortcut. |
| dofscroll | Bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch, toggle or hold joystick modes (`mode:next`, `mode:prev`, `mode:<id>`, `toggle:mode:<id>`, `hold:mode:<id>`), or work with the axis handling (`nav`, `toggle:nav`). Also: shaping scroll and zoom from the six axes, keys derived from axes (`keys:<neg>,<pos>`), per-program and per-device profiles. |

Examples: a button that always sends Esc belongs in the driver. A button that sends Space (play/pause) only while the `timeline` joystick mode is active, a button with `mode:next` and a button with `nav` belong in dofscroll.

A joystick mode may bind keys or clicks that only make sense in that joystick mode, for example Space for
play/pause in a timeline-style joystick mode. Such bindings are active only while that joystick mode is active.
This is what the driver cannot do on Linux, because `spnavrc` knows nothing about dofscroll's
joystick modes.

Why the driver is better for a mapping that is always the same: it works in every program and
keeps working when dofscroll is stopped. dofscroll's own `key:`, `click`, `rightclick` and
`middleclick` actions and the deprecated `CLICK_BUTTON` stay fully supported, also as an option for
people without a driver. The shipped examples use them only for joystick-mode-specific bindings.

### Linux: spacenavd

Edit `/etc/spnavrc` (the system-wide spacenavd config) and restart spacenavd, or send it SIGHUP.
Option names below are taken from spacenavd's upstream `doc/example-spnavrc`. A local man page
for `spnavrc` was not available when this was written, so check the file that came with your
version. Button numbers in `spnavrc` are zero-based, while dofscroll's `BUTTON_<n>` is
one-based.

- `kbmapN = <keysym>` maps button N to a keyboard key, using a keysym name from
  `/usr/include/X11/keysymdef.h` without the `XK_` prefix. Example: `kbmap0 = Escape`.
  Upstream marks this as experimental.
- `bnmapN = M` remaps button N to button M, and `bnactN = <action>` assigns spacenavd's own
  actions (for example `sensitivity-up`). These are not key or click mappings.
- Mouse clicks and shortcuts: this was not found in the upstream example file. Not verified.
  Without a driver option, dofscroll's `click` actions are the fallback.

### Windows and macOS: 3Dconnexion driver

The 3Dconnexion driver has a settings or control panel where buttons can be assigned to keys,
shortcuts or commands, per application. The exact menu names differ between driver versions and
were not verified here, so look for the button or "customize buttons" page in your driver's
settings. See [windows-mac.md](windows-mac.md) for what is known about the driver and the
port notes ([port-windows.md](port-windows.md), [port-macos.md](port-macos.md)).

### Avoiding double input

The vendor driver may itself map the axes to scroll or pan in some applications. If an
application then scrolls twice (once from the driver, once from dofscroll), switch dofscroll off
for that application (`ENABLED = 0` in its `[app:...]` profile). dofscroll is designed to run next
to the driver, which stays installed and running; see
[troubleshooting.md](troubleshooting.md#the-official-3dconnexion-driver-and-dofscroll). Details and
sources are in [windows-mac.md](windows-mac.md) and the `port-*.md` documents.

## Key names

Names are lower case.

| Group | Names |
|---|---|
| Modifiers | `ctrl`, `shift`, `alt`, `super` |
| Arrows and navigation | `up`, `down`, `left`, `right`, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete` |
| Editing | `space`, `enter`, `esc`, `tab`, `backspace` |
| Function keys | `f1` to `f24` (on macOS only `f1` to `f20`) |
| Letters and digits | `a` to `z`, `0` to `9` |
| Plus and minus | `plus`, `minus` (main keyboard), `kp_plus`, `kp_minus` (keypad) |
| Keypad | `kp_0` to `kp_9`, `kp_enter`, `kp_plus`, `kp_minus`, `kp_multiply`, `kp_divide`, `kp_decimal` |
| Punctuation | `comma`, `period`, `slash`, `backslash`, `semicolon`, `apostrophe`, `bracket_left`, `bracket_right`, `grave`, `equal` (the key at its US-layout place) |
| System keys | `caps_lock`, `print`, `scroll_lock`, `pause`, `menu`, `num_lock` |
| Media keys | `play_pause`, `media_next`, `media_prev`, `media_stop`, `volume_up`, `volume_down`, `volume_mute` |

On macOS Apple has no key codes for `f21` to `f24`, `print`, `scroll_lock`, `pause`, `num_lock` and the media keys, so these are not sent there (the other keys are).

`plus` is the main-keyboard plus. On X11 it is resolved from the keyboard layout. On uinput it
is sent as Shift and `=` (a US-style layout is assumed). `kp_plus` and `kp_minus` are the keypad keys and
do not depend on the layout. Programs differ in which of them they accept for zoom.

## Example: timeline joystick mode

For video editing (for example in DaVinci Resolve, whose behavior is not verified here):

    [app:resolve]
    MODE = timeline
    SCROLL_H_SENS = 4     # scrubbing
    ZOOM_SENS = 1         # single frames

The timeline must have keyboard focus. Reverse the twist with `ZOOM_SIGN = -1`.

### `step:<neg>,<pos>`: one key press per push

`keys:` turns the deflection into a repeat rate, which is right for scrubbing and wrong for
discrete targets such as slides: a firm push skips several, and holding it pages through the deck.
`step:` taps `<neg>` or `<pos>` (same grammar as `keys:`, chords allowed) exactly once per push:

    V_ACTION = step:pageup,pagedown

The channel goes through the same processing as for `keys:` (dead zone, curve, smoothing,
sensitivity, sign). The tap fires when the processed value, a rate in steps per second
(`deflection * SENS / 20` for a linear curve), reaches `STEP_TRIGGER` (default 20, see
[configuration](configuration.md)). It re-arms only after the value has fallen to half of the
trigger; there is no auto-repeat while the push is held. Pushing to the other side fires the other
key (each side re-arms on its own). The state resets on joystick mode change, reload and `releaseAll`.
Because the trigger is measured after `*_SENS`, a channel with a low sensitivity needs a lower
`STEP_TRIGGER`. The chord warning rules below apply to `step:` exactly as to `keys:`. How the
trigger level feels on a real device is not verified; tune `STEP_TRIGGER`.

### The key engine: analog and digital

Every motion (vertical and horizontal push and tilt separately, twist, lift; both directions) can press a key or chord per
direction, in a joystick mode and in a profile. `keys:` is **analog** (the key repeats faster the further you push),
`step:` is **digital** (one press per push, like a click). In the interface this is the check box "Analog (the key
repeats faster the further you push)"; it only switches `keys:` and `step:` in the stored text, the keys stay.

Per side the value is a key, a chord or `none`: `step:none,pagedown` presses nothing for the negative direction. At least
one side needs a key (use `none` for the whole action otherwise).

Three keys per motion tune the engine. `<P>` is `SCROLL_V_PUSH_`, `SCROLL_V_TILT_`, `SCROLL_H_PUSH_`, `SCROLL_H_TILT_`,
`ZOOM_` (the twist) or `LIFT_`. They are valid in a joystick mode file and in every profile section, like the other channel
keys. A channel that is not split (`V_ACTION`, `H_ACTION`) uses the push keys.

| Key | Meaning |
|---|---|
| `<P>KEY_RATE_MIN` | Analog: lowest repeat rate in presses per second while the motion is deflected (0..1000, default 0 = no lower limit). With a value above 0 the first press comes at once when the motion leaves rest. |
| `<P>KEY_RATE_MAX` | Analog: highest repeat rate (0..1000, default 0 = no limit). Whatever you set, the rate never exceeds `min(100, TICK_HZ / 2)` presses per second. |
| `<P>TRIGGER` | Digital: the level (steps per second after the channel processing, like `STEP_TRIGGER`, which is the default) that gives one press. Analog: with a trigger the motion counts as at rest below it and the first press comes at once when it is crossed; it rests again at half the trigger. |

The repeat rate is `clamp(|u|, KEY_RATE_MIN, KEY_RATE_MAX)`, `u` being the value of the motion in steps per second. Without
the three keys the behavior is the one of older versions, with one exception: at most **4 presses per tick and function and
8 per tick in total** are sent (older versions sent up to 200 per tick), which only matters for a very slow tick rate or an
extreme sensitivity. The interface starts a new analog function with 3 to 25 presses per second; these are written as the
two rate keys.

Digital: one press when `|u|` reaches the trigger, re-armed when it has fallen to half the trigger; the other direction fires
the other key and each side re-arms on its own. A push and a tilt are separate motions with their own keys, so tilting while
you push cannot press the push key twice, but the same jitter around the trigger of one motion is absorbed by the hysteresis.
Feel and thresholds of analog and digital, and the crosstalk of tilt against push with digital keys, are not verified on a real
device.

**Tier rule.** Analog (`keys:`) is only accepted for keys that are safe to repeat (tier A): the arrows, page up/down, home,
end, tab, `ctrl` or `shift` plus one of these, plus and minus (also with `ctrl` / `shift`), volume up and down, media next
and previous. Everything else (letters, digits, function keys, Enter, Delete, Backspace, Esc, space, `alt` or `super`
chords, play/pause, a lone modifier) is tier B: `keys:` is refused with "this key can only be used digitally, use step:",
`step:` is allowed with the load warning of the chord rules below. The lift never takes an analog key. The built-in `media`
mode therefore uses `step:j,l` for the twist (one jump per twist instead of a rate). The ready choices of the joystick mode
editor "Page up / Page down", "Switch tabs" (`keys:ctrl+pageup,ctrl+pagedown`), "Volume" (`keys:volume_up,volume_down`) and
"Change track" (`step:media_prev,media_next`) are taken from the built-in modes `slides`, `tabs`, `volume` and `tracks`.
The volume and media keys are not sent on macOS.

### Mouse clicks and holds

Every motion (both directions, in a joystick mode and in a profile) can click the mouse or hold something while it is
deflected. Per side the value of `step:` is a key, a chord, `none` or a mouse button word; `hold:` takes the same, with the
difference that a key or a chord may be only modifiers (`hold:ctrl+shift,none`).

| Text | What happens |
|---|---|
| `step:click,none` | One left click each time the motion is pushed past the trigger on the negative side, nothing on the other side. `rightclick` and `middleclick` are the other two buttons. |
| `hold:click,none` | The left button goes down when the motion is pushed past the trigger and comes up when it has fallen back to half of it: a drag while you hold the cap. |
| `hold:ctrl,alt` | Ctrl is held while the motion is deflected to the negative side, Alt while it is deflected to the positive side. |

The trigger is the one of the key engine (`<P>TRIGGER`, default `STEP_TRIGGER`), the re-arm happens at half of it, and each
side re-arms on its own, exactly as for `step:`. A click is one press and release of the button per crossing; a held button
is never clicked by another motion (its release would end the drag). `keys:` (analog) with a click is refused ("a mouse click
can only be used digitally"), and so is any analog key that is not safe to repeat.

Everything a hold keeps down is the **union** with what else wants it: a hold of `ctrl` on a tilt while the twist zooms (zoom
holds `ZOOM_MODIFIER`, default Ctrl) keeps Ctrl down until both have let go; two holds that share a modifier do the same, and a
button binding that holds the same key or mouse button joins them. A hold keeps following its motion while the twist zooms.
Nothing is let go on a timer: a hold longer than 60 seconds is logged once and stays, because a long drag is legitimate.
Everything a hold keeps down is released on a joystick mode change, a reload, suspend, `ENABLED = 0`, an unplugged device,
a source that goes silent (stale watchdog), the safety net and when a program profile unbinds the motion.

Mouse clicks and holds are tier B (they act on whatever has the focus): the plain load warning applies, there is no gate.
How click and hold feel on a real device, and a drag with a hold on macOS, are not verified.

### Macros

A macro is a list of steps that runs once per trigger: key down, key up and pause. It is digital only, so it sits on a device
button or on a motion direction, never on an analog key repeat. The steps are separated by spaces:

| Step | Meaning |
|---|---|
| `+<chord>` | Key down. A chord presses its modifiers in the order written, then the key (`+ctrl+c`); the key stays down until a step lets go of it. |
| `-<chord>` | Key up. Releases the key first, then the modifiers in the reverse order. |
| `wait:<ms>` | Pause of 1 to 60000 milliseconds. |

There is no "tap" step: pressing and releasing a key is two steps (`+a -a`). Key names are the closed list of `keys.hpp`, as
everywhere. Example (select all, copy, then type "ab" with shift held, 30 ms between the steps):

    # [app:editor]
    BUTTON_5 = macro:+ctrl +a wait:30 -a +c wait:30 -c -ctrl +shift wait:30 +a -a +b -b

On a **device button** the text after `macro:` is the macro that runs when the button is pressed. On a **motion** it is
`macro:<neg>,<pos>`: one macro per direction, run when the motion is pushed past the trigger on that side (the digital trigger of
`step:`, re-armed at half of it); a side may be `none`:

    H_TILT_ACTION = macro:+ctrl +shift +tab -tab -shift -ctrl,+ctrl +tab -tab -ctrl
    V_TILT_ACTION = macro:none,+ctrl +s -s -ctrl

**Release part.** A side can have a second macro that runs when the trigger is released (a button is let go; a motion has
fallen back to half of the trigger). The two parts are separated by `|`; `none` stands for a part without steps:

    BUTTON_6 = macro:+f5 -f5 | +f6 -f6            # F5 when the button goes down, F6 when it is let go
    BUTTON_7 = macro:none | +ctrl+a -ctrl+a wait:30 +ctrl+c -ctrl+c   # nothing on the press, select all and copy on the release
    V_PUSH_ACTION = macro:+a -a | +b -b,none      # a on the way in, b on the way back, only for the negative side

Rules:

- **Everything a macro holds is released.** When a macro ends, every key a step pressed and nobody let go of is released, so a
  press-only macro such as `+ctrl` or `+ctrl wait:500` is valid and leaves nothing stuck. The same happens at once, with the macro
  aborted, on a joystick mode change, a reload, suspend, `ENABLED = 0`, an unplugged device and "Release all keys"
  (the paths of `releaseAll()`), and the safety net clears whatever the output still holds after a macro. A key that something
  else holds as well (a button bound to it, the zoom modifier, a `hold:`) stays down until that lets go too. A change of the
  program does not abort a running macro (a macro that switches windows changes the program itself).
- **One macro at a time.** While a macro runs, further triggers (a button, a motion crossing the trigger again, a release part)
  are ignored until it is done; nothing is queued.
- **Timing.** The pauses are counted in the daemon's ticks (`TICK_HZ`, 250 per second by default), so a step runs in the first tick
  in which the pause is over (at most one tick late, 4 ms at 250 Hz); time that carries over shortens the next pause. Steps without a
  pause between them run in the same tick, in order. A macro in which a pause is the last step ends after that pause.
- **Limits.** At most 256 steps per macro part (pauses count as steps); the queue of a running macro is the same 256 steps.
  The action text is at most 32 KiB.
- **Safety net.** A running macro counts as activity for the safety net (a pause longer than its quiet period of 250 ms must not
  cut the macro); the net does its work as before once the macro is done.
- **Safety.** The keys come from the closed list of `keys.hpp`. A macro with tier B keys (letters, digits, function keys,
  Enter, Delete, Esc, space, `alt` or `super` chords, a lone modifier, ...) gets the plain load warning, there is no gate; see
  `docs/security.md`, section 7. A macro acts on whatever has the focus: only take over files you have read.

How macros behave in real programs (timing, recording, release at the end) is not verified on a real device.

An old daemon does not know `macro:`: it ignores the line with its usual warning, nothing else changes.

### Own functions made in the interface

The interface can compose any of the actions above for you: **New function ...** in the drop-down of a motion or a button of a
profile (see [ui.md](ui.md#new-function--and-the-own-functions-of-a-profile)). It writes the same action text into the profile
(`H_TILT_ACTION = keys:ctrl+pageup,ctrl+pagedown`, `BUTTON_3 = key:ctrl+0`) and the rate and trigger keys of the motion; there is
no other storage. **Save as joystick mode** copies such a function into `modes/<id>.conf`, **Save as button mode** into
`buttons/<id>.conf`.

### Key chords in `keys:`

Each of the two arguments is either a key or a chord: zero to four modifiers (`ctrl`, `shift`,
`alt`, `super`, each at most once, any order) joined by `+` to exactly one key that is not a
modifier, for example `keys:ctrl+pageup,ctrl+pagedown` (previous / next tab). `plus` and `minus`
are key names, so `ctrl+plus` is valid. A lone modifier (`step:ctrl,shift`) is valid as `step:` only.

One step presses the modifiers that are not held yet (in the order written), taps the key and
releases only the modifiers it pressed, in reverse order. A modifier that is already held, for
example Ctrl for zoom on another channel or a button with `key:ctrl`, stays held. Rate, dead zone,
curve, smoothing and sensitivity are unchanged. Buttons (`key:`) stay single keys.

## Split push and tilt

The vertical channel mixes push forward/back (`z`) and tilt forward/back (`rx`), the horizontal
channel push sideways (`x`) and tilt sideways (`rz`). With `V_ACTION` and `H_ACTION` both movements
do the same thing. A joystick mode can give them different functions by splitting a channel into a **push**
part and a **tilt** part:

| Key | Part | Default mix term |
|---|---|---|
| `V_PUSH_ACTION` | vertical, push forward/back | `z` |
| `V_TILT_ACTION` | vertical, tilt forward/back | `rx` |
| `H_PUSH_ACTION` | horizontal, push sideways | `x` |
| `H_TILT_ACTION` | horizontal, tilt sideways | `-rz` |

The values are the same as for `V_ACTION` (`default`, `none`, `wheel_v`, `wheel_h`, `zoom`, `pointer_x`, `pointer_y`,
`keys:<neg>,<pos>`, `step:<neg>,<pos>`, chords allowed).

Example: scroll with push, flip pages with tilt, zoom with the twist as usual:

    NAME = Pages
    DESCRIPTION = Push scrolls, tilt flips pages
    V_PUSH_ACTION = wheel_v
    V_TILT_ACTION = step:pageup,pagedown
    H_PUSH_ACTION = wheel_h
    H_TILT_ACTION = step:left,right
    SCROLL_V_TILT_SENS = 8          # per-part tuning, see below

**How the parts are derived.** They come from the channel's own mix expression (`SCROLL_V_MIX`,
`SCROLL_H_MIX`, from the profile or the joystick mode's overrides): the translation terms (`x`, `y`, `z`)
form the push part, the rotation terms (`rx`, `rz`) the tilt part, with the weights and signs you
configured. `0.5*rx + z` gives push `z` and tilt `0.5*rx`; `x - rz` gives push `x` and tilt `-rz`.
A mix without rotation terms has an empty tilt part (it never fires), one without translation terms
an empty push part. A twist term (`ry`) in a scroll mix belongs to neither part and is ignored in
a split channel. Lift/press (`y`) counts as push if you put it in the mix; it is in no default mix,
because grabbing and releasing the cap triggers it by accident.

**Compatibility.** A joystick mode that sets none of the four keys behaves exactly as before. If a joystick mode sets
at least one split key of a channel, that channel runs as two independent sub-channels, each with
its own smoothing, rate accumulator and `step:` state, and the mixed channel is not evaluated.
A part without its own key uses the channel's `V_ACTION` / `H_ACTION` (default `default`), so a joystick mode
can override only one part. An explicit `default` on a part means the channel's normal behavior
(the wheel), not `V_ACTION`. The other channel is unaffected: splitting V leaves H as it was.
A joystick mode with a split key is a custom-path joystick mode (like any joystick mode with a non-default action):
`NAV_BUTTON` / `CLICK_BUTTON` from the settings do not apply, the joystick mode's own buttons do (a button
with `nav` turns wheel parts into arrow keys, as for the channel).

**Per-part tuning.** Each part uses the sensitivity, dead zone, curve, smoothing and sign of its
parent channel (`SCROLL_V_SENS`, ...). To tune a part separately, set one or more of
`SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`, for example `SCROLL_V_TILT_SENS`
or `SCROLL_H_PUSH_DEADZONE`. These are **absolute values**, not multipliers: same syntax and ranges
as the parent keys, and a key that is not set keeps the parent's value (so changing the channel's
`SCROLL_V_SENS` still moves every part that has no value of its own). They are ordinary settings
keys: valid in a joystick mode file (where they belong) and in a profile section, and they act only while
the active joystick mode splits that channel. `STEP_TRIGGER` applies to every `step:` part and
is measured after the part's sensitivity, as for a channel.

Several parts can hold the zoom modifier (by default Ctrl) at once for `zoom` (it is released when the last of them is back at
rest). Everything a part holds is released on a joystick mode switch, a reload, an unplugged device,
suspend and the safety net, like the rest.

`zoom` works on any motion, a tilt included (`V_TILT_ACTION = zoom`, in a joystick mode file or in a profile). The part zooms
with the sensitivity, dead zone, sign and curve of its own channel and only from its own axes, so no dominance latch is
needed; while a part zooms, the other wheel actions (scroll, scroll sideways) pause, because the zoom modifier is held and they
would zoom too; keys and clicks are not paused. The twist keeps its own zoom with the dominance latch. Tilt forward sends the
wheel "down", which most programs read as zoom out; flip `SCROLL_V_SIGN` (or the sign of the part) for the other way.

Recipes for profiles (configuration.md): [use the tilt as zoom in one program](configuration.md#recipe-use-the-tilt-as-zoom-in-one-program),
[take the twist assignment from another profile](configuration.md#recipe-take-the-twist-assignment-from-another-profile) and
[build a profile from existing modes](configuration.md#recipe-build-a-profile-from-existing-modes) (a profile names a joystick mode with
`MODE = <id>` and a button mode with `BUTTON_<n> = buttonmode:<id>`, so it follows that mode; the profile builder can also make an
own copy of the mode instead).

What the tilt and push feel like (and whether `step:` triggers cleanly on one without the other
when you push diagonally) depends on the device and your hand and is not verified without hardware;
raise the dead zone of the part that fires by accident.

Validate a joystick mode file with `build/tests/check_config --modes <directory>` (source build only).

## Lift/press is unused

Lift and press of the cap (axis `y`) have an action of their own since the lift motion exists: `LIFT_ACTION`
(`none`, `wheel_v`, `wheel_h`, `zoom`, `pointer_x`, `pointer_y`, `step:<neg>,<pos>`; no analog `keys:`; the joystick mode editor offers the same list as
the profile's Motions page), tuned by `LIFT_MIX` (default `y`; the
Windows and macOS seeds say `z`), `LIFT_SENS`, `LIFT_DEADZONE` (default 80), `LIFT_SIGN`, `LIFT_CURVE` and
`LIFT_SMOOTH_MS`, all valid in a joystick mode file and in a profile. It is still deliberately not used by any default
or built-in joystick mode (the default action is `none`): grabbing or releasing the cap moves it up or down a little,
and a function on `y` would fire by accident every time. See [configuration.md](configuration.md#lift--press). The default mixes use push forward/back and tilt forward/back (`z`, `rx`)
for vertical scroll, both sideways movements (`x`, `rz`) for horizontal scroll and twist (`ry`)
for zoom; `ZOOM_LATERAL` lists `x z rx rz`. A joystick mode file must not put `y` into a
`*_MIX` or `ZOOM_LATERAL` override unless its `DESCRIPTION` says so (mention "lift/press").
You can still choose `y` for a channel in your own configuration; it stays prone to false
triggers, and the Channels page shows a warning next to an axis mix that uses `y`. In the joystick mode editor's
"Motions and buttons" tab lift/press can be added to a channel mix as a term (with a weight and the same warning); it
writes `SCROLL_V_MIX = z + rx + y` (or `SCROLL_H_MIX`, `ZOOM_MIX`) as an override of that joystick mode, nothing else.
`tests/test_no_lift_press.cpp` scans the built-in joystick modes, the seeded default
configurations and the documented defaults and fails when `y` appears there. The one exception is the
Windows default: the official 3Dconnexion driver names the axes in a different order (there `y` is push
forward/back, `z` is lift/press and `rz` the twist), so the Windows file uses `y`, `x` and `-rz`.
The Channels page warns about the axis your configuration leaves out of the channel mixes (`y` in the Linux
default, `z` in the Windows and macOS defaults), and only when it is sure which axis that is
(`src/ui/model/axis_roles_model.hpp`); the tables above show the Linux names.

## Switching a file off

A joystick mode file is only read when its name ends in `.conf`. To keep a file without using it, rename
`<id>.conf` to `<id>.conf.disabled` (or use the switch on the User modes page of the interface, which does exactly
that). The daemon then ignores the file; if its id is that of a built-in joystick mode, the built-in one applies
again. Button mode files in `buttons/` work the same way.

## Safety and sharing joystick modes

A joystick mode file is code-like. While a device button is held or an axis is deflected, it can press
any key on the list in [Key names](#key-names), including modifiers (Ctrl, Alt, Super), function
keys, Delete and Enter. Chords are reachable: Ctrl is held while a channel zooms, so a `keys:`
action on another channel can send Ctrl+W or Ctrl+Q, and `key:ctrl` plus `key:alt` on two buttons
combine with a third key. A joystick mode file cannot type arbitrary text or send codes outside the key list,
and every effect needs the physical device to be moved or pressed.

Chords in `keys:` (for example `keys:ctrl+w,ctrl+q`) are sent as written. The daemon warns about
a chord that contains `alt` or `super`, or that combines `ctrl` / `shift` with a key that is not a
navigation key (arrows, Page Up/Down, Home, End, Space, Enter, Esc, and the zoom keys plus, minus and `0`). A chord of `ctrl`
and/or `shift` with a navigation key, like `ctrl+pageup` or the zoom reset `ctrl+0`, produces no warning. The same
rule applies to `key:<chord>` button actions in joystick mode files and button mode files. The button modes that ship with the
program (examples/buttons) are not checked: only files in your folders.

Only use joystick mode files from sources you trust, and read a file before you copy it into
`~/.config/dofscroll/modes/`. When you review one, look for:

- `key:` bindings, especially `ctrl`, `alt`, `super`, `shift`, `delete`, `esc`, `enter` and `f1` to `f24`.
- `keys:<negative>,<positive>` actions on `V_ACTION`, `H_ACTION` or `TWIST_ACTION`, especially
  letters and digits, or in combination with a `zoom` channel (the zoom modifier, by default Ctrl, is held then).
- `zoom` on any channel, because it holds the zoom modifier (by default Ctrl).
- `mode:` bindings that switch to another joystick mode, and the other files such a joystick mode points to.
- Settings overrides that change sensitivities, signs or button behavior.

The daemon warns at load time (in the log) about modifier-capable bindings, but the warning does not
replace reading the file. If you share a joystick mode, say what keys it presses.

A joystick mode that turns output off or ignores the device (for example the built-in `off`) leaves the
device inactive. To get out of it, use the tray menu "Joystick mode", a `mode:next` or `mode:<id>` binding
(put it in the main config so it stays available in every joystick mode, or in the joystick mode itself), or edit
`MODE` in the config and reload.

## Add your own joystick mode

1. Create `~/.config/dofscroll/modes/<id>.conf` with the keys above.
2. Wait about a second (automatic reload), or run `systemctl --user kill -s HUP dofscroll.service`.
3. Select it with `MODE = <id>` in a profile, from the tray "Joystick mode" menu, or with a `mode:<id>`
   button action.
4. Check `journalctl --user -u dofscroll.service` for warnings about skipped lines.

In the interface the "Split push and tilt" switch of a channel in the in-page editor does the same (see `docs/ui.md`).

The interface can do the same without a text editor: on the Joystick modes page, "Edit" opens an in-page
editor for name, channels, buttons and overrides (see [ui.md](ui.md#editing-a-joystick-mode)); its first tab shows
the cap with the six motions and the device buttons and lets you assign an action by clicking one (push and tilt
with the same action are written as `V_ACTION` / `H_ACTION`, different actions as the split keys; reversing a
direction writes the sign, for example `SCROLL_V_SIGN = -1`). The file stays plain text; the editor keeps
comments and lines it does not know.

## Editing joystick modes from the tray

The tray "Joystick mode" submenu lists, below the joystick mode choices, one entry per joystick mode:

- `Edit mode file: <name>` opens the joystick mode's file in the default editor.
- `Create editable copy of <name>...` appears for built-in joystick modes that have no file yet (normal,
  off, timeline). It writes a commented template `<id>.conf` with the built-in's name, description
  and channel actions into the joystick modes folder (file mode 0600, never overwriting an existing file) and
  opens it. A file of the same name replaces the built-in joystick mode.
- `Open modes folder` opens the folder in the file manager.

The editor is started outside the tray's sandbox, like "Open config file". After saving, the
daemon reloads the joystick modes by itself and the new entry appears in the menu within about a second.

The built-in joystick modes are good starting points: "Create editable copy" and change it.
