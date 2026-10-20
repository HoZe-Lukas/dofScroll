<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# How to write a joystick mode

This page is a tutorial: it shows how to create your own **joystick mode** (what the three channels do) and your own **button mode** (what one device button does), what is possible, and gives recipes you can copy. You do not need to know the code. Everything here was checked against the program source; what could only be checked on paper and not on a real device is marked "not verified on hardware". The short reference is [Modes](Modes); the list of ready-made modes is [Built-in modes](Built-in-modes).

Contents: [1. Files](#1-where-mode-files-live) - [2. Channels](#2-the-three-channels-and-what-drives-them) - [3. Channel actions](#3-channel-actions) (incl. [split push and tilt](#split-push-and-tilt)) - [4. Recipes](#4-recipes) - [5. Tuning keys](#5-tuning-keys-in-a-mode) - [6. Button modes](#6-button-modes) - [7. Buttons inside a joystick mode](#7-buttons-inside-a-joystick-mode) - [8. Testing](#8-testing-a-mode) - [9. Safety](#9-safety) - [10. Sharing](#10-sharing-a-mode)

## 1. Where joystick mode files live

A joystick mode is one plain text file. The **file name without `.conf` is the joystick mode id** (`[a-z0-9_-]`, 1 to 32 characters, lower case; some names that are reserved device names on Windows, such as `con` or `nul`, are refused on every system). `timeline.conf` is the joystick mode `timeline`.

| System | Joystick modes | Button modes |
|---|---|---|
| Linux | `~/.config/dofscroll/modes/<id>.conf` (`$XDG_CONFIG_HOME` is used if set to an absolute path) | `~/.config/dofscroll/buttons/<id>.conf` |
| Windows | `%APPDATA%\dofscroll\modes\<id>.conf` | `%APPDATA%\dofscroll\buttons\<id>.conf` |
| macOS | `~/Library/Application Support/dofscroll/modes/<id>.conf` | `~/Library/Application Support/dofscroll/buttons/<id>.conf` |

(The Windows and macOS programs are built by CI and not run on hardware; their folders are taken from the documentation.)

- **How the program finds them.** It reads every `*.conf` file in these two folders, about once a second it checks whether the contents changed, and it reloads when they did. Limits: up to 64 files per folder, each up to 64 KiB, regular files owned by you. A file that breaks a rule is skipped with a warning in the log.
- **Reload.** Saving a file is enough. You can also force a reload: on Linux `systemctl --user kill -s HUP dofscroll.service`; saving in the tray or in the interface notifies the daemon too. A reload clears runtime switches (toggles, a joystick mode chosen by a button or from the tray menu).
- **Overriding a built-in.** A file whose id equals a built-in joystick mode (`normal`, `off`, `timeline`, `slides`, `media`, `map`, `precision`, `tabs`, `browse`, or the built-in button modes) replaces it for as long as the file exists. Delete the file and the built-in comes back. The interface and tray have "Create editable copy", which writes such a file for you: the best way to start.
- **Editor or text editor.** In the interface (page Modes, "Edit") an in-page editor has tabs for name, the three channels, the buttons and the settings overrides; it checks the file with the same loader as the daemon, keeps your comments and unknown lines, and "Open in text editor" is always there. Button modes have their own page (Button modes). Everything the editor does is a plain text file, so you can switch between the two at any time. The tray "Mode" submenu also offers "Edit joystick mode file", "Create editable copy" and "Open joystick modes folder".
- **Choosing the joystick mode.** `MODE = <id>` in `[global]`, `[device:*]` or `[app:*]` of the config, the joystick mode drop-down on the Profiles page, the tray "Joystick mode" menu, or a button (`mode:<id>`, `toggle:mode:<id>`, `hold:mode:<id>`, see [section 7](#7-buttons-inside-a-joystick-mode)). Without anything the joystick mode is `normal`.

### File format

    NAME = Timeline (scrub / single frames)    # display name (optional, at most 64 bytes shown)
    DESCRIPTION = Sideways = scrub              # one line (optional, at most 256 bytes shown)
    V_ACTION = none
    H_ACTION = keys:left,right
    TWIST_ACTION = keys:left,right
    BUTTON_1 = key:space
    SCROLL_H_SENS = 4

- One `KEY = value` per line, no `[sections]`. Lines starting with `#` or `;` are comments; `#` after a space ends a value.
- Keys are case-insensitive in a joystick mode file (write them in capitals anyway), action values are lower-cased.
- A joystick mode file may contain: `NAME`, `DESCRIPTION`, `V_ACTION`, `H_ACTION`, `TWIST_ACTION`, `BUTTON_<n>` and any number of **settings overrides** (section 5). Not allowed: `MODE`, `ENABLED`, `BACKEND`, `TICK_HZ` (a warning, the line is ignored).
- Anything you leave out stays at its default: a channel you do not mention is `default`.
- File format: UTF-8, no BOM needed.

## 2. The three channels and what drives them

The device has six axes. dofscroll mixes them into three **channels**:

| Channel | Joystick mode key | Tuning prefix | Default mix | In words |
|---|---|---|---|---|
| Vertical (V) | `V_ACTION` | `SCROLL_V_` | `z + rx` | push the cap forward/back (`z`) plus tilt it forward/back (`rx`) |
| Horizontal (H) | `H_ACTION` | `SCROLL_H_` | `x - rz` | push sideways (`x`) plus tilt it sideways (`rz`, with opposite sign in the mix) |
| Twist (zoom) | `TWIST_ACTION` | `ZOOM_` | `ry` | twist the cap |

The axis names (libspnav convention, measured on a SpaceMouse Pro): `x` sideways push, `y` lift or press, `z` push forward or back, `rx` tilt forward or back, `ry` twist, `rz` tilt sideways. Full deflection is about 350 raw units.

**Lift and press (`y`) is unused on purpose.** It is in no default mix and no built-in joystick mode puts a function on it, because lifting or pressing the cap while you grab or release it would cause false triggers (maintainer decision, [issue #94](https://github.com/HoZe-Lukas/dofScroll/issues/94)). You can still write `y` in a mix; expect false triggers.

If you changed axis settings in `spacenavd` (swap, inversion), the axes may not be what is described here. The axis assistant ("Learn axes") on the Axes page finds out which physical movement drives which axis.

### Signs and directions

Each channel produces a signed value (a rate, see below). What negative and positive mean depends on the channel and the action (checked in the daemon code):

| Channel | Positive value | Negative value |
|---|---|---|
| V | scroll **down** (wheel down), second key of `keys:` / `step:` | scroll up, first key |
| H | scroll **right**, second key of `keys:` / `step:` | scroll left, first key |
| Twist | **zoom in** (Ctrl + wheel up), second key of `keys:` / `step:` | zoom out, first key |

So `keys:<neg>,<pos>` always reads "first key for negative, second key for positive". On a vertical channel `keys:pageup,pagedown` means: negative = Page Up, positive = Page Down.

Details per action:

- `wheel_v` on V or H: positive = wheel down. `wheel_v` on the **twist**: positive = wheel up (inverted, because twist positive means zoom in). `wheel_h`: positive = right.
- `zoom` on V or H: Ctrl is held and the wheel turns with the channel's sign (positive = wheel down, which most programs read as zoom out). On the twist, positive = wheel up = zoom in.
- Which physical direction gives a positive value is not fixed by this page (it depends on the device and on `spacenavd`; not verified for every model). Find out with `--debug` (section 8) or flip the direction with `*_SIGN = -1`.

### Rate and steps

Each channel is processed in this order: mix of axes, times `*_SIGN`, optional smoothing, dead zone, curve, then sensitivity. The result is a **rate in steps per second**:

    rate = (|mix| - DEADZONE) * SENS / 20      (linear curve, no smoothing)

A "step" is one wheel notch, or one key tap for `keys:`. For example with the defaults (`SCROLL_V_SENS = 5`, dead zone 30), a push of 200 raw units gives (200 - 30) * 5 / 20 = about 42 steps per second. The faster you push, the faster it repeats: that is the analog behavior of `wheel_*`, `zoom` and `keys:`. `step:` is the exception: it taps once per push instead (section 3).

## 3. Channel actions

Set per channel with `V_ACTION`, `H_ACTION` or `TWIST_ACTION`.

| Value | Effect | Example |
|---|---|---|
| `default` | The channel's normal behavior: V is the vertical wheel, H the horizontal wheel, twist is zoom (Ctrl + wheel, only when the twist dominates the other axes) | `H_ACTION = default` |
| `none` | Channel off | `V_ACTION = none` |
| `wheel_v` | Vertical mouse wheel | `H_ACTION = wheel_v` |
| `wheel_h` | Horizontal mouse wheel | `V_ACTION = wheel_h` |
| `zoom` | Holds Ctrl and sends the vertical wheel | `V_ACTION = zoom` |
| `keys:<neg>,<pos>` | Key taps at the channel's rate, `<neg>` for negative, `<pos>` for positive deflection | `V_ACTION = keys:pageup,pagedown` |
| `step:<neg>,<pos>` | One key tap per push (no repeat while held) | `V_ACTION = step:pageup,pagedown` |

Rules:

- If **all three** actions are `default` (or not written), the joystick mode runs the normal path: dominance-based zoom, and the older `NAV_BUTTON` and `CLICK_BUTTON` settings still work. As soon as one channel is not `default`, the channels follow the joystick mode's actions, and the older `NAV_BUTTON` / `CLICK_BUTTON` are not used: bind buttons with `BUTTON_<n> = nav` or `click` instead (section 7).
- In a joystick mode with a non-default channel, a `default` channel means its natural action (V = `wheel_v`, H = `wheel_h`, twist = `zoom`).
- Twist = `zoom` (or `default`) keeps the dominance detection: zoom only starts when the twist is stronger than the sideways, push and tilt axes (`ZOOM_LATERAL`, `ZOOM_ENTER_MS`, `ZOOM_EXIT_RATIO`). `zoom` on V or H has no such detection: Ctrl is held whenever that channel is deflected.
- **Ctrl is shared.** While any channel zooms, Ctrl is held for the whole output, so a wheel event of another channel in the same moment is also a Ctrl+wheel.
- The `nav` button (section 7) turns `wheel_v` / `wheel_h` (also as `default`) into arrow keys while held. It does not change `keys:` or `step:` channels.
- An invalid value is a warning (`V_ACTION: ... - line ignored`) and the channel stays `default`.

### `keys:` and the key names

Keys are lower case. The full list (`src/keys.hpp`):

| Group | Names |
|---|---|
| Modifiers | `ctrl`, `shift`, `alt`, `super` |
| Arrows and navigation | `up`, `down`, `left`, `right`, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete` |
| Editing | `space`, `enter`, `esc`, `tab`, `backspace` |
| Function keys | `f1` to `f12` |
| Letters and digits | `a` to `z`, `0` to `9` |
| Plus and minus | `plus`, `minus` (main keyboard), `kp_plus`, `kp_minus` (keypad) |

There are no media keys (volume, play) and no way to type text. `plus` is resolved from the keyboard layout on X11; with the uinput backend it is sent as Shift and `=` (US-style layout assumed). `kp_plus` / `kp_minus` do not depend on the layout. Programs differ in which of them they accept for zoom.

**Chords.** Each of the two arguments may be a chord: zero to four modifiers (`ctrl`, `shift`, `alt`, `super`, each at most once, any order) joined by `+` to exactly one key that is not a modifier. `keys:ctrl+pageup,ctrl+pagedown` is previous and next tab. `ctrl+plus` is valid. A lone modifier (`keys:ctrl,shift`) is also valid. One tap presses the modifiers that are not yet held, taps the key and releases only the modifiers it pressed. A modifier that is already held (a `key:ctrl` button, or zoom) stays held.

Exactly two arguments, separated by one comma, are required: `keys:left` is an error.

### `step:` and `STEP_TRIGGER`

`keys:` repeats, which is right for scrubbing and wrong for discrete targets such as slides: a firm push skips several, and holding it pages through the deck. `step:` taps `<neg>` or `<pos>` exactly once per push. It goes through the same processing (dead zone, curve, smoothing, sensitivity, sign). The tap fires when the rate reaches `STEP_TRIGGER` (default 20 steps per second, range 0.5 to 10000), and the action re-arms when the rate has fallen to half of it. Pushing to the other side fires the other key. Switching mode or reloading resets it.

Because the trigger is compared after the sensitivity, how far you must push is (linear curve):

    deflection = DEADZONE + STEP_TRIGGER * 20 / SENS

With the vertical defaults (dead zone 30, sensitivity 5, trigger 20) that is 110 raw units. A channel with a low `*_SENS` needs a lower `STEP_TRIGGER` (a global key, so it can be set in the joystick mode file too). How the trigger level feels on a real device is not verified on hardware: tune it.

### Split push and tilt

The vertical channel mixes push forward/back (`z`) and tilt forward/back (`rx`); the horizontal channel mixes push sideways (`x`) and tilt sideways (`rz`). With `V_ACTION` / `H_ACTION` both movements do the same. To give them different functions, split a channel into a **push** part and a **tilt** part:

| Key | Part | Default mix term |
|---|---|---|
| `V_PUSH_ACTION` | vertical, push forward/back | `z` |
| `V_TILT_ACTION` | vertical, tilt forward/back | `rx` |
| `H_PUSH_ACTION` | horizontal, push sideways | `x` |
| `H_TILT_ACTION` | horizontal, tilt sideways | `-rz` |

The values are the same as for `V_ACTION` (`default`, `none`, `wheel_v`, `wheel_h`, `zoom`, `keys:`, `step:`, chords allowed). Example: push scrolls, tilt flips pages, twist zooms as usual:

    NAME = Pages
    DESCRIPTION = Push scrolls, tilt flips pages
    V_PUSH_ACTION = wheel_v
    V_TILT_ACTION = step:pageup,pagedown
    H_PUSH_ACTION = wheel_h
    H_TILT_ACTION = step:left,right
    SCROLL_V_TILT_SENS = 8

How it works:

- The parts come from the channel's own mix (`SCROLL_V_MIX`, `SCROLL_H_MIX`): translation terms (`x`, `y`, `z`) are the push part, rotation terms (`rx`, `rz`) the tilt part, with your weights and signs. A mix without rotation terms has an empty tilt part (it never fires). A twist term `ry` in a scroll mix belongs to neither part and is ignored in a split channel.
- A joystick mode that sets none of the four keys behaves as before. If a joystick mode sets at least one of a channel's keys, that channel runs as two independent sub-channels (own smoothing, own rate accumulator, own `step:` state) and the mixed channel is not evaluated. A part without its own key uses `V_ACTION` / `H_ACTION`; an explicit `default` on a part means the normal wheel, not `V_ACTION`. Splitting V leaves H unchanged.
- Signs are as for the channel: positive = down / right, second key of `keys:` / `step:`.
- Tuning: each part uses its channel's sensitivity, dead zone, curve, smoothing and sign. To tune one part separately set `SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`, for example `SCROLL_V_TILT_SENS = 8`. These are **absolute values**, not multipliers, with the same ranges as the channel keys; a key you do not set keeps the parent's value. They act only while the active joystick mode splits that channel. `STEP_TRIGGER` applies to every `step:` part, measured after that part's sensitivity.
- A joystick mode with a split key is a non-default mode: `NAV_BUTTON` / `CLICK_BUTTON` do not apply, `BUTTON_<n> = nav` turns wheel parts into arrow keys.
- The in-page editor has a "Split push and tilt" switch per channel (two action rows and a tuning box per part, see `docs/ui.md`); the Axes page shows the push and tilt part of a split channel live; you can also write the keys in a text editor. How push and tilt feel apart on a real device, and whether `step:` on one triggers cleanly without the other, is not verified on hardware.

## 4. Recipes

Each recipe is a complete joystick mode file or a few lines for one. Save it as `~/.config/dofscroll/modes/<id>.conf` (or the folder of your system, section 1), then select the joystick mode. Key meaning always depends on the program; none of these is verified in every program or on hardware.

### Horizontal scrolling

Everything scrolls sideways: up/down movement (V) becomes the horizontal wheel, the sideways channel is off.

    NAME = Sideways scroll
    DESCRIPTION = Push and tilt forward/back scroll horizontally
    V_ACTION = wheel_h
    H_ACTION = none

Twist as horizontal scroll, the rest unchanged:

    NAME = Twist scrolls sideways
    TWIST_ACTION = wheel_h

(Tuning of the twist is `ZOOM_*`, for example `ZOOM_SENS`.) Horizontal scroll with the tilt only, sideways push off: give the horizontal channel a mix of only `rz`:

    NAME = Tilt scrolls sideways
    H_ACTION = wheel_h
    SCROLL_H_MIX = - rz

`- rz` is the tilt part of the default mix `x - rz`; use `SCROLL_H_MIX = x` for the sideways push only.

### Vertical scroll only

    NAME = Vertical only
    DESCRIPTION = Reading documents: only up/down scrolls
    V_ACTION = wheel_v
    H_ACTION = none
    TWIST_ACTION = none

### Swap or invert directions

Invert one channel with its sign; this also reverses `keys:` and `step:` on that channel:

    SCROLL_V_SIGN = -1     # vertical scroll the other way
    SCROLL_H_SIGN = -1     # horizontal the other way
    ZOOM_SIGN = -1         # twist the other way

With `keys:` you can instead swap the two arguments: `keys:pagedown,pageup`.

To make the vertical channel scroll sideways and the horizontal channel scroll vertically, swap the actions: `V_ACTION = wheel_h`, `H_ACTION = wheel_v`.

### Page up and page down

    V_ACTION = keys:pageup,pagedown        # repeats while pushed
    V_ACTION = step:pageup,pagedown        # one page per push (slides)
    SCROLL_V_SENS = 10                     # with step: about 70 raw units of push needed

### Arrow-key navigation

    NAME = Arrows
    V_ACTION = keys:up,down
    H_ACTION = keys:left,right
    TWIST_ACTION = none

The built-in `map` is this plus `minus` / `plus` on the twist.

### Zoom on a channel

Zoom with the vertical channel (push or tilt forward/back):

    V_ACTION = zoom
    H_ACTION = none
    TWIST_ACTION = none
    SCROLL_V_SIGN = -1     # if "forward" zooms out in your program

Zoom with the twist is the default. For zoom by keys, give the twist plus and minus: `TWIST_ACTION = keys:minus,plus`.

### Scrub a timeline

    NAME = Scrub
    DESCRIPTION = Sideways = scrub, twist = single frames
    V_ACTION = none
    H_ACTION = keys:left,right
    TWIST_ACTION = step:left,right
    SCROLL_H_SENS = 4
    BUTTON_1 = key:space       # play/pause only while this joystick mode is active

It is the built-in `timeline` with single frames on `step:` (one frame per twist). The timeline must have keyboard focus.

### Switch tabs

    H_ACTION = keys:ctrl+pageup,ctrl+pagedown

Vertical scroll and zoom stay as they are (built-in `tabs`). Use `step:ctrl+pageup,ctrl+pagedown` for exactly one tab per push.

### Volume and seek

    NAME = Player
    V_ACTION = keys:up,down        # volume in many players
    H_ACTION = keys:left,right     # seek
    TWIST_ACTION = keys:j,l        # 10 seconds back/forward on YouTube

(Built-in `media`.) These are ordinary keys; which key does what is up to the program.

### Slow down

Everything finer: lower sensitivities, a bigger dead zone, a curve that is soft near the middle, smoothing.

    NAME = Gentle
    SCROLL_V_SENS = 2
    SCROLL_H_SENS = 1
    ZOOM_SENS = 2
    SCROLL_V_DEADZONE = 50
    SCROLL_V_CURVE = 1.5       # exponent above 1: finer at light pressure
    SCROLL_V_SMOOTH_MS = 80    # smoother, a little slower to react

Built-in `precision` has just the three sensitivities. To get slow only while a button is held, use `BUTTON_3 = hold:mode:precision` (section 7). For a point list: `SCROLL_V_CURVE = 0:0 50:0.1 200:1 350:3` (input after the dead zone : factor; ascending; linear interpolation).

### Disable a channel

    V_ACTION = none        # nothing happens on this channel
    SCROLL_V_SENS = 0      # also off (sensitivity 0 means off)

### Only one active axis

Only push forward/back scrolls; every other movement is ignored:

    NAME = Push only
    DESCRIPTION = Only push forward/back scrolls
    V_ACTION = wheel_v
    H_ACTION = none
    TWIST_ACTION = none
    SCROLL_V_MIX = z

Same idea for others: only the twist (`V_ACTION = none`, `H_ACTION = none`, twist `default`), or a single tilt axis with `SCROLL_V_MIX = rx`. Mixes may weigh terms (`SCROLL_V_MIX = z + 0.5*rx`).

## 5. Tuning keys in a joystick mode

Every key of the [Configuration reference](Configuration-reference) except `MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` can be written in a joystick mode file as an **override**. The loader checks each one with the same parser as the main config; an invalid one is a warning and the line is ignored. At most 128 overrides per file (more and the whole file is ignored). The channel keys exist for each channel prefix `SCROLL_V_`, `SCROLL_H_` and `ZOOM_`:

| Key | Range and unit | Meaning |
|---|---|---|
| `<prefix>MIX` | sum of `[+-][weight*]axis`, axes `x y z rx ry rz`, weight 0 to 100, up to 12 terms; empty = channel gives 0 | Which axes feed the channel |
| `<prefix>SENS` | integer 0 to 255 (values outside are clamped); 0 = channel off | Sensitivity; rate = value * SENS / 20 |
| `<prefix>DEADZONE` | integer, 0 or more, raw units (full deflection is about 350) | Ignore small values; the part above it counts |
| `<prefix>SIGN` | `1` or `-1` | Direction |
| `<prefix>CURVE` | exponent 0.1 to 10, or a point list `in:factor ...` (inputs ascending, up to 64 points, factor up to 100) | Response curve (reference input 200) |
| `<prefix>SMOOTH_MS` | 0 to 500 ms, 0 = off | Low-pass time constant |
| `SCROLL_<V\|H>_<PUSH\|TILT>_<SENS\|DEADZONE\|SIGN\|CURVE\|SMOOTH_MS>` | as the channel keys | Per-part tuning of a split channel (absolute values; unset = parent's value) |
| `STEP_TRIGGER` | 0.5 to 10000 steps per second | Level that fires a `step:` action |
| `ZOOM_LATERAL`, `ZOOM_ENTER_MS`, `ZOOM_EXIT_RATIO` | axes list, ms, number of at least 1.0 | Twist dominance detection (zoom with the twist) |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED`, `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | older keys | Only used by a joystick mode whose channels are all `default` |

**Inheritance.** A joystick mode override is not a replacement of the profile, it sits in the middle of the chain. Lowest to highest: `[global]` < matching `[device:*]` < overrides of the active **joystick mode** < matching `[app:*]`. So an override in a joystick mode beats the device value, and an `[app:*]` section can still have the last word. All device sections apply before all app sections. The joystick mode is picked by `MODE` of the resolved profile, so an app section can select a joystick mode and still change that joystick mode's values. See [Profiles](Profiles).

Only the keys you write are overridden; everything else is inherited from the profile.

## 6. Button modes

A **button mode** is one reusable behavior for a device button, in its own file in the `buttons/` folder (section 1). The id is the file name. A button mode can be assigned to any button of any profile; the same one to several buttons. It says what a button does, not what the axes do.

    NAME = Navigation latch
    DESCRIPTION = Press once to switch navigation on, again to switch it off
    ACTION = toggle:nav

Keys of a button mode file: `NAME` (shown in the drop-downs, fallback is the id), `DESCRIPTION`, `ACTION` (required) and `CYCLE` (optional). Any other key is skipped with a warning, a file without a valid `ACTION` is skipped.

### The ACTION grammar

`ACTION` is one button action, the same grammar as `BUTTON_<n>` (a `buttonmode:` reference inside a button mode is refused: no nesting):

| Action | Effect |
|---|---|
| `none` | Nothing |
| `click`, `rightclick`, `middleclick` | Mouse button, held while the device button is held |
| `key:<name>` | Holds that key while the button is held |
| `key:<mod>+<key>` | A chord such as `key:ctrl+0`: zero to four of `ctrl`, `shift`, `alt`, `super`, then a key; tapped once per press, nothing on release |
| `hold:mode:<id>` | Joystick mode `<id>` is effective while the button is held |
| `nav` | While held, X/Y send arrow keys instead of scrolling |
| `toggle:nav` | Every press switches navigation on or off |
| `mode:<id>` | Switch to joystick mode `<id>` on press |
| `mode:next`, `mode:prev` | Next or previous joystick mode in alphabetical order of id (built-ins included), wrapping |
| `toggle:mode:<id>` | First press switches to `<id>`, the next goes back to the profile's joystick mode (`next` / `prev` not allowed) |
| `hold:mode:<id>` | Joystick mode `<id>` only while the button is held (`next` / `prev` not allowed) |
| `buttonmode:<id>` | Only in `BUTTON_<n>`, not inside a button mode file |

Remember the design rule: a button that always sends the same key or click belongs in the vendor driver, not in dofscroll. Button modes are for joystick mode switching, latches, cycles and bindings that depend on dofscroll's state ([Buttons](Buttons)). `hold:mode:<id>` is accepted in the `ACTION` of a button mode by the loader, although the table in `docs/button-modes.md` does not list it yet (checked in `src/button_modes.cpp`, not tested on hardware).

### Cycle through your own list: `CYCLE`

    NAME = Next favorite joystick mode
    ACTION = mode:next
    CYCLE = timeline, normal, slides

`CYCLE` is only valid together with `ACTION = mode:next` or `mode:prev`. It is a list of joystick mode ids separated by commas (spaces are fine, case is folded), at most 32; a duplicate is dropped with a warning; an invalid id makes the line invalid, and the button then cycles through all joystick modes. `next` goes to the entry after the current joystick mode and wraps; `prev` goes back. If the current joystick mode is not in the list, `next` goes to the first entry and `prev` to the last. Ids that match no loaded mode are skipped (the log says `mode cycle: unknown mode '<id>' skipped`). One file has one direction: for both directions make two button modes with the same list.

Without `CYCLE`, `mode:next` walks through all loaded joystick modes alphabetically, built-ins included, so the cycle is long.

### Naming and using

Give it a clear `NAME` (this is what the drop-downs show) and a one-line `DESCRIPTION` (tooltip). In the interface: Profiles page, the drop-down under each button lists the button modes by name, then the plain actions; or write it by hand:

    [global]
    BUTTON_3 = buttonmode:latch-nav
    BUTTON_4 = buttonmode:cycle-favorites

An unknown id is logged once (`unknown button mode '<id>'`) and the button does nothing; the interface shows `missing: <id>`.

### Button numbers

`BUTTON_<n>` is **1-based** (`BUTTON_1` is the first button), at most 32, `BUTTON_0` is rejected. The number of buttons you can configure is the factory count of the device model (SpaceMouse Pro 15, Compact 2, Enterprise 31), not the count that `spacenavd` reports. `dofscroll --debug` prints `Button <i> pressed` with the raw index `<i>` as the device reports it, which starts at 0: the number for `BUTTON_<n>` is `<i> + 1`. The Axes page of the interface shows the buttons the daemon sees as well.

### Built-in button modes

`latch-nav`, `next-mode`, `prev-mode`, `cycle-favorites`: see [Built-in modes](Built-in-modes#button-modes). Create an editable copy to start from one. More detail: [docs/button-modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/button-modes.md).

## 7. Buttons inside a joystick mode

A joystick mode can bind buttons itself with `BUTTON_<n> = <action>` (the same actions as above, plus `buttonmode:<id>`). These bindings apply **only while that joystick mode is active**, which the vendor driver cannot do:

    BUTTON_1 = key:space              # play/pause in this joystick mode only
    BUTTON_3 = mode:normal            # leave this joystick mode
    BUTTON_4 = buttonmode:latch-nav

Precedence: a binding in the active joystick mode wins over `BUTTON_<n>` in the main config for the same button. Binding in the main config (for example `BUTTON_2 = mode:next`) works in every joystick mode that does not bind that button. A joystick mode binding with an unknown target mode is dropped with a warning.

Toggles and switches by a button are runtime state: the status file shows them, a reload (saving, the tray's "Release all keys"), a restart, suspend, `ENABLED = 0` or any joystick mode change clears them.

## 8. Testing a joystick mode

1. **Save and look at the log.** Warnings name the file and line. Linux: `journalctl --user -u dofscroll.service -f`; Windows: `%LOCALAPPDATA%\dofscroll\dofscroll.log`; the interface shows the log with "Show log...". The "Profile" and "Mode" lines tell what the daemon runs.
2. **Switch to it.** Tray "Joystick mode" menu, or "Active joystick mode" on the Overview page of the interface: both switch the running daemon until the next reload, without touching the config. Or a button, or `MODE = <id>` in a profile.
3. **Watch the numbers.** Stop the service first (otherwise events are sent twice) and run `dofscroll --debug` in a terminal. About ten times a second it prints `h=... v=... twist=... lateral=... zoom=... mode=<id> profile=[...]` while the cap is deflected: `h` and `v` are the signed rates of the horizontal and vertical channel (positive = right / down; for a split channel the mixed value stays idle and `v_push`, `v_tilt`, `h_push`, `h_tilt` show the parts), `twist` is the magnitude of the twist channel, `zoom` says whether zoom latched, `nav` appears while navigation is on. It also prints `Button <i> pressed` with the raw, 0-based index: use `<i> + 1` in `BUTTON_<n>`. That tells you the sign of a movement and which values `STEP_TRIGGER` and the dead zone have to beat.
4. **Axes page.** The Axes page of the interface shows live bars for the six axes and button cells. While it is open, the daemon pauses its output, so moving the cap does not scroll. It shows the device, not your joystick mode: use it to see which axis a movement drives.
5. **Validation without the daemon.** `check_config --modes DIR` (built with the project, in `build/tests/`) loads a folder of joystick mode files with the daemon's loader and exits with 1 on any warning, 0 with `OK: ...`; `check_config FILE` validates a main config file. The interface editor lists the same problems and keeps Save disabled until they are gone. Contributors: `cmake --build build --target check-fast` parses every embedded example mode.

### Typical mistakes and the messages you get

Messages start with `<file>:<line>:` in the log.

| Message (shortened) | Cause |
|---|---|
| `sections are not supported in mode files - ignored` | A `[section]` line. Use plain `KEY = value` |
| `line without '=' ignored` | A typo, or a continuation line |
| `V_ACTION: expected default \| none \| wheel_v \| ...` | Unknown action word, or a typo in it |
| `keys:<neg>,<pos> expects exactly two keys or key chords` | One key, three keys, or a space instead of a comma |
| `unknown key 'x'` | A key name that is not in the list (section 3), e.g. `pgup` instead of `pageup` |
| `'x' is not a modifier (ctrl, shift, alt, super)` / `a chord must end with a key that is not a modifier` / `modifier 'ctrl' appears twice` | Chord syntax |
| `MODE is not allowed in a mode file - line ignored` | Same for `ENABLED`, `BACKEND`, `TICK_HZ` (`... (global setting)`) |
| `BUTTON_0: buttons are numbered from 1 - line ignored` | Numbering starts at 1 |
| `button number must be 1..32` | Number out of range |
| `<KEY>: <reason> - line ignored` (e.g. `unknown key 'SCROLL_X_SENS'`) | A setting that does not exist, or an invalid value |
| `too many settings overrides (max. 128) - file ignored` | Too many overrides |
| `mode <id>: BUTTON_<n>: unknown mode '<x>' - binding ignored` | A `mode:` / `toggle:mode:` / `hold:mode:` target that is not loaded |
| `mode <id> can press shortcut chords (...) - read the file before use` | Not an error: the file is loaded; see section 9 |
| `... not a regular file - ignored`, `... file too large (max. ...)`, `... not owned by the current user - ignored`, `too many mode files (max. 64)` | File limits |
| `invalid UTF-8 at byte N - shown as U+FFFD` | Save the file as UTF-8 |
| Button modes: `ACTION: ... - line ignored`, `no valid ACTION - file ignored`, `CYCLE needs ACTION = mode:next or mode:prev - ignored`, `<KEY>: not allowed in a button mode file (NAME, DESCRIPTION, ACTION, CYCLE)` | Button mode problems |

If nothing happens at all, check in this order: is the joystick mode really active (log line `Mode`), does the key name work in that program, is `ENABLED` 1, is the channel's `*_SENS` 0, is your push beyond the dead zone (and the `STEP_TRIGGER` level for `step:`).

## 9. Safety

A joystick mode file is code-like. While a button is held or an axis is deflected it can press any key of the list in section 3, including `ctrl`, `alt`, `super`, `delete`, `enter` and function keys. Chords are reachable: Ctrl is held while a channel zooms, so a `keys:` action on another channel can send Ctrl+W or Ctrl+Q, and `keys:` accepts chords itself. A joystick mode file cannot type text or send codes outside the key list, and every effect needs the device to be moved or pressed.

- Keep keys to navigation (arrows, page keys, home/end, space) where you can. Avoid letters and digits on an axis (a light push then types), and chords with `alt` or `super`.
- The daemon warns at load time about chords with `alt` or `super`, about `ctrl` / `shift` chords with a non-navigation key, about modifier keys on buttons or channels, and about `zoom` combined with non-navigation `keys:`. No warning does not mean harmless.
- If you share a joystick mode, say in the `DESCRIPTION` and in your post which keys it presses. If you receive one, read it before putting it into your joystick modes folder: look for `key:`, `keys:`, `step:`, `zoom`, `mode:` and settings overrides.
- A joystick mode that turns output off (like `off`) leaves the device inactive. Get out through the tray "Joystick mode" menu, a `mode:<id>` button bound in the main config, or by editing `MODE` and reloading. Do not bind a way out only inside the joystick mode you cannot leave.

More: [Security](Security).

## 10. Sharing a joystick mode

- **Propose an idea** with the **Joystick or button mode proposal** issue form: https://github.com/HoZe-Lukas/dofScroll/issues/new/choose (say joystick or button mode, the program, what the channels or the button should do, and whether you tested it).
- **Send the file** with a pull request that uses the "new mode" template (`?template=new-mode.md`): joystick modes go to `examples/modes/<id>.conf`, button modes to `examples/buttons/<id>.conf`, with the SPDX header, `NAME` and a short `DESCRIPTION`. The checklist and the design rules (analog response on joystick modes, no binding that is always the same) are on [Mode requests and contributions](Contributing-modes).

A merged file is built into the program with the next release and shows up as "Built in".
