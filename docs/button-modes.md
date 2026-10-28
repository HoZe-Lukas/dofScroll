<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Button modes

A **button mode** is a named button behavior in its own file. It is stored with the profile, can be
used with any joystick mode, and the same button mode can be assigned to several buttons. In the
interface every button of a profile has a drop-down that lists the button modes by name (Profiles
page); the Button modes page (category Joystick modes) creates, edits, duplicates, imports and deletes them.

A button mode only describes what a button does. What the axes do is the *joystick mode*
(see [modes.md](modes.md)). The principle of [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver)
stays: an action that is the same everywhere and always belongs in the vendor driver.

## Files

Location: `~/.config/dofscroll/buttons/<id>.conf` (next to `modes/`; on Windows and macOS the
configuration directory of the platform). The id is the file name without `.conf`, `[a-z0-9_-]`, at
most 32 characters. The format is that of the main configuration: `KEY = value`, no sections, `#` and
`;` comments, UTF-8 (see [encoding.md](encoding.md)).

| Key | Meaning |
|---|---|
| `NAME` | Shown in the drop-downs. Fallback: the id. |
| `DESCRIPTION` | Optional, shown as a tooltip and on the Button modes page. |
| `ACTION` | Required. One button action as in `BUTTON_<n>`: `none`, `click`, `rightclick`, `middleclick`, `nav`, `key:<name>`, `key:<mod>+...+<key>` (a chord such as `key:ctrl+0`, tapped once per press), `mode:<id>`, `mode:next`, `mode:prev`, `toggle:nav`, `toggle:mode:<id>`, `hold:mode:<id>`. It must not be a `buttonmode:` reference (no nesting). |
| `CYCLE` | Optional, only with `ACTION = mode:next` or `mode:prev`: the joystick modes this button cycles through, see [Choosing the joystick modes to cycle through](#choosing-the-modes-to-cycle-through). |

Every other key (`NAME`, `DESCRIPTION`, `ACTION` and `CYCLE` are the only ones) is skipped with a warning; a file without a valid `ACTION` is skipped. Example
(`examples/buttons/latch-nav.conf`):

    NAME = Navigation latch
    DESCRIPTION = Press once to switch navigation on, again to switch it off
    ACTION = toggle:nav

Limits and safety are those of joystick mode files: at most 64 files of at most 64 KiB, regular files owned
by the current user, opened without following links; invalid lines and files are skipped with a
warning in the log (`Button modes: ...`). The folder is checked about once a second and reloaded
when its contents change.

Five button modes are built into the program on every system (they need no file; nothing is
copied into `~/.config/dofscroll/buttons/`): `latch-nav` (`toggle:nav`), `next-mode` (`mode:next`),
`prev-mode` (`mode:prev`), `cycle-favorites` (`mode:next` through a list you choose with `CYCLE`) and `reset-zoom` (`key:ctrl+0`). Their text is `examples/buttons/<id>.conf`, embedded at build time. The
button mode catalog lists them with the origin "Built in". "Create editable copy" writes a file with
the same id that replaces the built-in one until you delete it; a file byte-identical to the built-in
is not listed as your own. `next-mode` and `prev-mode` cycle through all loaded joystick modes in
alphabetical order, built-ins included (see [modes.md](modes.md)).

`reset-zoom` (`key:ctrl+0`) is built in too: each press taps Ctrl+0 once, which resets the zoom in most programs (browsers, editors, image viewers). It is a reusable named behavior that a profile or a joystick mode chooses per button; if you want Ctrl+0 on a button everywhere and always, the vendor driver can do that as well.

## Choosing the joystick modes to cycle through

Without `CYCLE`, `mode:next` and `mode:prev` walk through all loaded joystick modes in alphabetical
order of id, so the order is arbitrary and grows with every joystick mode you add. With `CYCLE` the button mode
cycles through exactly the joystick modes you list, in your order:

    NAME = Next favorite joystick mode
    ACTION = mode:next
    CYCLE = timeline, normal, slides

- The value is a comma separated list of joystick mode ids (whitespace is tolerated, case is folded,
  ids as in `MODE`), at most 32. A duplicate is dropped with a warning; an invalid id makes the line
  invalid (warning, the button mode then cycles through all joystick modes).
- `next` goes to the entry after the effective joystick mode and wraps from the last to the first; `prev` goes
  the other way. If the effective joystick mode is not in the list (the profile's joystick mode, a `hold:mode:` or
  `mode:<id>` override), `next` goes to the first entry and `prev` to the last.
- An id that matches no loaded joystick mode stays in the file and is skipped; the daemon logs
  `mode cycle: unknown mode '<id>' skipped` once per load.
- One file has one direction. For both directions make two button modes (`ACTION = mode:next` and
  `ACTION = mode:prev`) with the same list, and assign them to two buttons. You can have several lists
  (for example *Next video joystick mode* and *Next browse joystick mode*) and choose one per button on the Profiles page.
- In the interface: on the Button modes page, with the action "Next joystick mode" or "Previous joystick mode",
  the section **Cycle through** chooses between all joystick modes and "Only these joystick modes": tick the
  modes, order them with "Move up" / "Move down" (the page writes the `CYCLE` line; no tick or "All"
  removes it). Ids that match no loaded joystick mode are shown as "missing: <id>" and kept. The built-in
  button modes have no file: `next-mode` and `prev-mode` go through all joystick modes, `cycle-favorites`
  through `timeline`, `normal`; "Create editable copy" lets you change that.
- A raw `BUTTON_<n> = mode:next` and a button mode without `CYCLE` keep cycling through all joystick modes.
  Internally the daemon expands a button mode with `CYCLE` to `mode:next:<id>,<id>,...` (also accepted
  in `BUTTON_<n>` and joystick mode files, but the button mode is the supported way). `CYCLE` with another action
  is ignored with a warning. Example: `examples/buttons/cycle-favorites.conf`.

## Using a button mode

The button action `buttonmode:<id>` in the usual `BUTTON_<n>` syntax means "behave like the action
in that file":

    [global]
    BUTTON_3 = buttonmode:latch-nav
    [app:resolve]
    BUTTON_3 = buttonmode:latch-nav
    BUTTON_5 = buttonmode:latch-nav       # the same button mode on a second button

It works wherever `BUTTON_<n>` works: in `[global]`, `[device:*]`, `[app:*]` and in joystick mode
files. **The precedence does not change**: a binding in the active joystick mode file wins over `BUTTON_<n>`
of `[global]` and `[device:*]`, but a `BUTTON_<n>` of an `[app:*]` profile wins over the joystick mode; the profile order is
global < device < joystick mode overrides < app.

The daemon replaces the reference by the action of the file when it applies a profile; this
happens at runtime only. The configuration and the joystick mode files keep the text `buttonmode:<id>`, and the
interface shows and saves it as it is.

An unknown id (no such file) is logged once (`unknown button mode '<id>'`) and the button does
nothing. In the interface such a reference is shown as `missing: <id>` and is not rewritten.
Deleting a button mode in the interface lists where it is still used first.

## In the interface

- **Profiles page:** a *Joystick mode* drop-down (what the axes do, on the General tab) and a
  drop-down per button (Motions and buttons tab: select a button). Its entries: *Default (inherit from Global)* (device and
  program profiles; in Global *Nothing*), the button modes by name (tooltip: the description), then
  the plain actions (navigation, joystick mode switch, toggles, and behind "Advanced" key and
  click). *Default (inherit from Global)* removes the profile's own `BUTTON_<n>` line.
- **Button modes page:** list, an editor for name, description and action (the same action
  choices as on the Profiles page, without "button mode"), New, New from example, Duplicate, Import, Delete and "Open button modes folder". There is no
  Save button: a change is written by itself (a drop-down at once, typed text after a pause). The id never changes when you edit. A button mode can press
  a key or click: import only files you trust, the import dialog shows the loader warnings first.
