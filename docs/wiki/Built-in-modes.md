<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Built-in joystick modes

A **joystick mode** decides what the three channels (vertical, horizontal, twist) do and can override settings; a **button mode** is one reusable behavior for a device button (a joystick mode switch, a latch, a cycle list). Both kinds ship inside the program on every system: nothing has to be copied anywhere, the text of most of them is in [examples/modes/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/modes) and [examples/buttons/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/buttons) and is embedded at build time (`normal`, `off` and `timeline` are defined in [src/modes.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/modes.cpp)). For writing your own, see [Joystick modes](Modes); to propose or contribute one, see [Joystick mode requests and contributions](Contributing-modes).

## How to use one

- **Joystick mode:** pick it in the profile's joystick mode drop-down in the interface (Profiles page), from the tray "Joystick mode" menu (switches the running daemon until the next reload; it does not write `MODE`), or write `MODE = <id>` into `[global]`, `[device:*]` or `[app:*]` of the config, for example `[app:impress]` with `MODE = slides`. A button can switch to it with `BUTTON_<n> = mode:<id>` (or `hold:mode:<id>`, `toggle:mode:<id>`).
- **Button mode:** pick it in the button's drop-down in the interface, or write `BUTTON_<n> = buttonmode:<id>`.
- **Override:** a file with the same id in `~/.config/dofscroll/modes/` or `~/.config/dofscroll/buttons/` replaces the built-in ("Create editable copy" in the interface writes one for you).
- **Cycling:** `mode:next` and `mode:prev` (and the button modes `next-mode` and `prev-mode`) walk through all loaded joystick modes in alphabetical order of id, built-ins included, wrapping around. A button mode with a `CYCLE` list (`cycle-favorites`, or your own) cycles through exactly the listed joystick modes in your order instead.

The one-line DESCRIPTION of every built-in joystick mode and button mode says in plain words what it does and for which program it is meant; the interface shows it as a tooltip and in the detail view. Several joystick modes below press keys. Key meaning depends on the program and none of them has been verified in every program; see the safety note on the [Joystick modes](Modes) page.

## Joystick modes

Every joystick mode is described by what each of the six motions of the cap does (see [The six motions](Modes#the-six-motions)). Push and tilt of a channel do the same in every built-in joystick mode (none of them splits a channel), and lift / press is not used by any of them.

| Joystick mode | Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press | Sensitivity overrides |
|---|---|---|---|---|---|---|---|
| `normal` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | none |
| `off` | Off | Off | Off | Off | Off | Not used | none |
| `timeline` | Off | Off | Keys: Left / Right | Keys: Left / Right | Keys: Left / Right | Not used | none |
| `slides` | Keys: Page Up / Page Down | Keys: Page Up / Page Down | Off | Off | Off | Not used | SCROLL_V_SENS = 2 |
| `media` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: J / L | Not used | none |
| `map` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Minus / Plus | Not used | none |
| `precision` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | SCROLL_V_SENS = 2, SCROLL_H_SENS = 1, ZOOM_SENS = 2 |
| `tabs` | Scroll | Scroll | Keys: Ctrl+Page Up / Ctrl+Page Down | Keys: Ctrl+Page Up / Ctrl+Page Down | Zoom | Not used | none |
| `browse` | Keys: Page Up / Page Down | Keys: Page Up / Page Down | Keys: Left / Right | Keys: Left / Right | Zoom | Not used | none |
| `sheet` | Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Page Up / Page Down | Not used | none |
| `photo` | Zoom | Zoom | Keys: Left / Right | Keys: Left / Right | Off | Not used | none |
| `zoom-pan` | Zoom | Zoom | Scroll sideways | Scroll sideways | Scroll sideways | Not used | none |
| `vertical-only` | Scroll | Scroll | Off | Off | Off | Not used | none |
| `fast` | Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used | SCROLL_V_SENS = 12, SCROLL_H_SENS = 10, ZOOM_SENS = 12 |

In the table, `Scroll` is the vertical mouse wheel, `Scroll sideways` the horizontal one and `Zoom` is Ctrl plus the wheel. Key actions (`Keys: A / B`, `keys:` in the file) press the negative and the positive key as the axis is pushed, at a rate that follows the deflection. `default` is the channel's normal behavior (scroll wheel, Ctrl+wheel for the twist). None of the built-in joystick modes binds buttons, so every button keeps what the main config gives it (use a button mode or `BUTTON_<n>` in the config).

### `normal`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used |

It is the joystick mode that is active when nothing else is selected; `NAV_BUTTON` still works. Good for general use. No overrides. No button bindings.

### `off`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Off | Off | Off | Off | Off | Not used |

Switches the device off without `ENABLED = 0`; leave it through the tray "Joystick mode" menu, a `mode:next` or `mode:<id>` binding in the main config, or by editing `MODE`. Good for a program or device profile where the device must stay quiet. No button bindings.

### `timeline`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Off | Off | Keys: Left / Right | Keys: Left / Right | Keys: Left / Right | Not used |

Good for stepping and scrubbing in video editors and similar timelines, for example `[app:resolve]` with `MODE = timeline` (DaVinci Resolve's behavior is not verified here). Defined in code; no overrides. No button bindings.

### `slides`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Keys: Page Up / Page Down | Keys: Page Up / Page Down | Off | Off | Off | Not used |

Override `SCROLL_V_SENS = 2` (gentler than the default of 5, so a push does not race through the deck). Good for presentations (LibreOffice Impress, PDF viewers in presentation mode), for example `[app:impress]` with `MODE = slides`. No button bindings.

### `media`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: J / L | Not used |

Good for video players and web video: up/down is volume in most players, sideways seeks, `j` / `l` go back / forward 10 seconds on YouTube. The keys are program-specific. No button bindings.

### `map`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Minus / Plus | Not used |

Good for maps and anything that understands arrow keys and plus/minus. `plus` and `minus` are the main-keyboard keys and depend on the keyboard layout. No button bindings.

### `precision`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used |

Overrides `SCROLL_V_SENS = 2`, `SCROLL_H_SENS = 1`, `ZOOM_SENS = 2` (the defaults are 5, 3 and 5). Good for fine control, per program or device with `MODE = precision`, or only while a button is held with `BUTTON_<n> = hold:mode:precision`. Counterpart: `fast`. No button bindings.

### `tabs`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Scroll | Scroll | Keys: Ctrl+Page Up / Ctrl+Page Down | Keys: Ctrl+Page Up / Ctrl+Page Down | Zoom | Not used |

Good for switching to the previous / next tab in browsers and terminals while up/down still scrolls and the twist still zooms. It presses a Ctrl chord (not verified in every program). No button bindings.

### `browse`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Keys: Page Up / Page Down | Keys: Page Up / Page Down | Keys: Left / Right | Keys: Left / Right | Zoom | Not used |

Good for paging through documents and slides: up/down pages through the document, sideways moves between pages, twisting zooms with Ctrl plus the mouse wheel. No button bindings.

### `sheet`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Keys: Up / Down | Keys: Up / Down | Keys: Left / Right | Keys: Left / Right | Keys: Page Up / Page Down | Not used |

Good for moving the cell cursor in spreadsheets. Not verified in a spreadsheet program. No button bindings.

### `photo`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Zoom | Zoom | Keys: Left / Right | Keys: Left / Right | Off | Not used |

Good for stepping through images and zooming in photo viewers and editors. Not verified in a photo program. No button bindings.

### `zoom-pan`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Zoom | Zoom | Scroll sideways | Scroll sideways | Scroll sideways | Not used |

Good for audio and video timelines: zoom with the vertical push or tilt, pan sideways by twisting. Not verified in a DAW or video editor. No button bindings.

### `vertical-only`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Scroll | Scroll | Off | Off | Off | Not used |

Good for reading documents without accidental sideways scroll or zoom, for example `[app:evince]` with `MODE = vertical-only`. No button bindings.

### `fast`

| Vertical push | Vertical tilt | Horizontal push | Horizontal tilt | Twist | Lift / press |
|---|---|---|---|---|---|
| Scroll | Scroll | Scroll sideways | Scroll sideways | Zoom | Not used |

Overrides `SCROLL_V_SENS = 12`, `SCROLL_H_SENS = 10`, `ZOOM_SENS = 12`. The counterpart of `precision`. Good for quickly scrolling long documents, logs and timelines. No button bindings.

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

ACTION `toggle:nav`. DESCRIPTION: "Navigation makes the cap work like arrow keys: push or tilt presses Up, Down, Left or Right instead of scrolling (to move a cursor or a selection). Press once to switch it on, press again to switch it off.". One press switches navigation on (the axes press arrow keys instead of scrolling), the next press switches it off.

Good for: a latched navigation mode instead of holding the `nav` button.

### `next-mode`

ACTION `mode:next`, no `CYCLE`. DESCRIPTION: "Each press switches to the next joystick mode in alphabetical order of their ids (built-in and your own) and starts over after the last. To choose which modes it visits, use 'Next favorite mode'.". Each press switches to the next joystick mode in alphabetical order of id, wrapping around.

Good for: trying all joystick modes, or a quick switch when you have only a few joystick modes. Add a `CYCLE` list in a copy to restrict it.

### `prev-mode`

ACTION `mode:prev`, no `CYCLE`. DESCRIPTION: "Each press switches to the previous joystick mode in alphabetical order of their ids (built-in and your own) and wraps around at the first. To choose which modes it visits, copy it and set a cycle list.". The mirror of `next-mode`: the previous joystick mode in alphabetical order, wrapping around.

Good for: the second button of a pair with `next-mode`.

### `cycle-favorites`

ACTION `mode:next`, `CYCLE = timeline, normal`. DESCRIPTION: "Each press switches to the next joystick mode of a short list you choose (by default timeline, then normal) instead of walking through all of them. Make an editable copy to change the list.". Each press goes to the next entry of the list, in that order, wrapping around; if the current joystick mode is not in the list the first entry is used. Unknown ids are skipped. For the opposite direction use a copy with `ACTION = mode:prev` and the same list.

Good for: a short list of your own favorite joystick modes. Edit the list in an editable copy (the built-in list is only a starting point).

### `reset-zoom`

ACTION `key:ctrl+0`. DESCRIPTION: "Press to send Ctrl+0, which sets the zoom back to 100 % in most programs (browsers, editors, image viewers). One tap per press.". Each press taps Ctrl+0 once; holding the button does not repeat it.

Good for: a button that puts the zoom of the current program back to 100%. It is a named behavior that a profile or a joystick mode picks per button; if you want Ctrl+0 on a button everywhere and always, the vendor driver can do that too. Whether Ctrl+0 resets the zoom depends on the program.
