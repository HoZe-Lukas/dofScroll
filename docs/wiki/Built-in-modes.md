<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Built-in joystick modes

A **joystick mode** decides what the three channels (vertical, horizontal, twist) do and can override settings; a **button mode** is one reusable behavior for a device button (a joystick mode switch, a latch, a cycle list). Both kinds ship inside the program on every system: nothing has to be copied anywhere, the text of most of them is in [examples/modes/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/modes) and [examples/buttons/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/buttons) and is embedded at build time (`normal`, `off` and `timeline` are defined in [src/modes.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/modes.cpp)). For writing your own, see [Joystick modes](Modes); to propose or contribute one, see [Joystick mode requests and contributions](Contributing-modes).

## How to use one

- **Joystick mode:** pick it in the profile's joystick mode drop-down in the interface (Profiles page), from the tray "Joystick mode" menu (sets the global joystick mode), or write `MODE = <id>` into `[global]`, `[device:*]` or `[app:*]` of the config, for example `[app:impress]` with `MODE = slides`. A button can switch to it with `BUTTON_<n> = mode:<id>` (or `hold:mode:<id>`, `toggle:mode:<id>`).
- **Button mode:** pick it in the button's drop-down in the interface, or write `BUTTON_<n> = buttonmode:<id>`.
- **Override:** a file with the same id in `~/.config/dofscroll/modes/` or `~/.config/dofscroll/buttons/` replaces the built-in ("Create editable copy" in the interface writes one for you).
- **Cycling:** `mode:next` and `mode:prev` (and the button modes `next-mode` and `prev-mode`) walk through all loaded joystick modes in alphabetical order of id, built-ins included, wrapping around. A button mode with a `CYCLE` list (`cycle-favorites`, or your own) cycles through exactly the listed joystick modes in your order instead.

Several joystick modes below press keys. Key meaning depends on the program and none of them has been verified in every program; see the safety note on the [Joystick modes](Modes) page.

## Joystick modes

Overview:

| Joystick mode | V (up/down) | H (sideways) | Twist | Good for |
|---|---|---|---|---|
| `normal` | default | default | default | everyday scroll and zoom |
| `off` | none | none | none | switching the device off |
| `timeline` | none | `left` / `right` keys | `left` / `right` keys | video editing |
| `slides` | `pageup` / `pagedown` keys | none | none | presentations |
| `media` | `up` / `down` keys | `left` / `right` keys | `j` / `l` keys | video players, web video |
| `map` | `up` / `down` keys | `left` / `right` keys | `minus` / `plus` keys | maps |
| `precision` | default | default | default | fine control (low sensitivity) |
| `tabs` | default | `ctrl+pageup` / `ctrl+pagedown` keys | default | switching tabs |
| `browse` | `pageup` / `pagedown` keys | `left` / `right` keys | zoom | documents, slides |

Key actions (`keys:`) press the negative and the positive key as the axis is pushed, at a rate that follows the deflection. `default` is the channel's normal behavior (scroll wheel, Ctrl+wheel for the twist). None of the built-in joystick modes binds buttons, so every button keeps what the main config gives it (use a button mode or `BUTTON_<n>` in the config).

### `normal`

All channels `default`, no overrides. DESCRIPTION: "Scroll, zoom and navigation as configured". It is the joystick mode that is active when nothing else is selected; `NAV_BUTTON` still works.

Good for: general use. No button bindings.

### `off`

All three channels `none`. DESCRIPTION: "Sends nothing". Switches the device off without `ENABLED = 0`; leave it through the tray "Joystick mode" menu, a `mode:next` or `mode:<id>` binding in the main config, or by editing `MODE`.

Good for: a program or device profile where the device must stay quiet. No button bindings.

### `timeline`

| Channel | Action |
|---|---|
| V | `none` |
| H | `keys:left,right` |
| Twist | `keys:left,right` |

No overrides. DESCRIPTION: "Sideways = scrub, twist = single frames". Defined in code. It has no button bindings of its own (a key that is always the same belongs in the vendor driver; a mode-dependent one can go into a joystick mode file of your own).

Good for: stepping and scrubbing in video editors and similar timelines, for example `[app:resolve]` with `MODE = timeline` (DaVinci Resolve's behavior is not verified here).

### `slides`

| Channel | Action |
|---|---|
| V | `keys:pageup,pagedown` |
| H | `none` |
| Twist | `none` |

Override: `SCROLL_V_SENS = 2` (gentler than the default of 5, so a push does not race through the deck). DESCRIPTION: "Up/down = page up/down, other channels off". No button bindings.

Good for: presentations (LibreOffice Impress, PDF viewers in presentation mode); assign it to the program with `[app:impress]` and `MODE = slides`.

### `media`

| Channel | Action |
|---|---|
| V | `keys:up,down` |
| H | `keys:left,right` |
| Twist | `keys:j,l` |

No overrides. DESCRIPTION: "Up/down = volume, sideways = seek, twist = j/l". No button bindings.

Good for: video players and web video; up/down is volume in most players, sideways seeks, `j` / `l` go back / forward 10 seconds on YouTube. The keys are program-specific.

### `map`

| Channel | Action |
|---|---|
| V | `keys:up,down` |
| H | `keys:left,right` |
| Twist | `keys:minus,plus` |

No overrides. DESCRIPTION: "Up/down and sideways = arrow keys, twist = minus/plus". No button bindings.

Good for: maps and anything that understands arrow keys and plus/minus. `plus` and `minus` are the main-keyboard keys and depend on the keyboard layout (see the key names on the [Joystick modes](Modes) page).

### `precision`

| Channel | Action |
|---|---|
| V | `default` |
| H | `default` |
| Twist | `default` |

Overrides: `SCROLL_V_SENS = 2`, `SCROLL_H_SENS = 1`, `ZOOM_SENS = 2` (defaults are 5, 3 and 5). DESCRIPTION: "Normal scroll and zoom with low sensitivity". No button bindings.

Good for: fine control, per program or device with `MODE = precision`, or only while a button is held with `BUTTON_<n> = hold:mode:precision`.

About a "fast" counterpart: `precision` is built in. A matching `fast` joystick mode (the same behavior with high sensitivity) is only an idea so far, see [issue #90](https://github.com/HoZe-Lukas/dofScroll/issues/90). Until then raise the sensitivities in a joystick mode file of your own.

### `tabs`

| Channel | Action |
|---|---|
| V | `default` |
| H | `keys:ctrl+pageup,ctrl+pagedown` |
| Twist | `default` |

No overrides. DESCRIPTION: "Sideways = Ctrl+Page Up / Ctrl+Page Down". No button bindings.

Good for: switching to the previous / next tab in browsers and terminals while up/down still scrolls and the twist still zooms. It presses a Ctrl chord (not verified in every program).

### `browse`

| Channel | Action |
|---|---|
| V | `keys:pageup,pagedown` |
| H | `keys:left,right` |
| Twist | `zoom` |

No overrides. DESCRIPTION: "Up/down = page up/down, sideways = left/right, twist = zoom". No button bindings.

Good for: paging through documents and slides: push up/down pages through the document, sideways moves between pages, twisting zooms with Ctrl + mouse wheel.

## Button modes

A button mode has one `ACTION` (one button action as in `BUTTON_<n>`, see [Buttons](Buttons)) and an optional `CYCLE` list. Full format: [docs/button-modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/button-modes.md).

| Button mode | Action | CYCLE |
|---|---|---|
| `latch-nav` | `toggle:nav` | none |
| `next-mode` | `mode:next` | none (all joystick modes, alphabetical) |
| `prev-mode` | `mode:prev` | none (all joystick modes, alphabetical) |
| `cycle-favorites` | `mode:next` | `timeline, normal` |
| `reset-zoom` | `key:ctrl+0` | none |

### `latch-nav`

ACTION `toggle:nav`. DESCRIPTION: "Press once to switch navigation on, press again to switch it off". One press switches navigation on (the axes press arrow keys instead of scrolling), the next press switches it off.

Good for: a latched navigation mode instead of holding the `nav` button.

### `next-mode`

ACTION `mode:next`, no `CYCLE`. DESCRIPTION: "Switches to the next joystick mode". Each press switches to the next joystick mode in alphabetical order of id, wrapping around.

Good for: trying all joystick modes, or a quick switch when you have only a few modes. Add a `CYCLE` list in a copy to restrict it.

### `prev-mode`

ACTION `mode:prev`, no `CYCLE`. DESCRIPTION: "Switches to the previous joystick mode". The mirror of `next-mode`: the previous joystick mode in alphabetical order, wrapping around.

Good for: the second button of a pair with `next-mode`.

### `cycle-favorites`

ACTION `mode:next`, `CYCLE = timeline, normal`. DESCRIPTION: "Cycles through a chosen list of joystick modes". Each press goes to the next entry of the list, in that order, wrapping around; if the current joystick mode is not in the list the first entry is used. Unknown ids are skipped. For the opposite direction use a copy with `ACTION = mode:prev` and the same list.

Good for: a short list of your own favorite modes. Edit the list in an editable copy (the built-in list is only a starting point).

### `reset-zoom`

ACTION `key:ctrl+0`. DESCRIPTION: "Sends Ctrl+0: resets the zoom in most programs (browsers, editors, image viewers)". Each press taps Ctrl+0 once; holding the button does not repeat it.

Good for: a button that puts the zoom of the current program back to 100%. It is a named behavior that a profile or a joystick mode picks per button; if you want Ctrl+0 on a button everywhere and always, the vendor driver can do that too. Whether Ctrl+0 resets the zoom depends on the program.
