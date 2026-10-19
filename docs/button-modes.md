<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Button modes

A **button mode** is a named button behavior in its own file. It is stored with the profile, can be
used with any joystick mode, and the same button mode can be assigned to several buttons. In the
interface every button of a profile has a drop-down that lists the button modes by name (Profiles
page); the Button modes page (category Modes) creates, edits, duplicates, imports and deletes them.

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
| `ACTION` | Required. One button action as in `BUTTON_<n>`: `none`, `click`, `rightclick`, `middleclick`, `nav`, `key:<name>`, `mode:<id>`, `mode:next`, `mode:prev`, `toggle:nav`, `toggle:mode:<id>`. It must not be a `buttonmode:` reference (no nesting). |

Every other key is skipped with a warning; a file without a valid `ACTION` is skipped. Example
(`examples/buttons/latch-nav.conf`):

    NAME = Navigation latch
    DESCRIPTION = Press once to switch navigation on, again to switch it off
    ACTION = toggle:nav

Limits and safety are those of mode files: at most 64 files of at most 64 KiB, regular files owned
by the current user, opened without following links; invalid lines and files are skipped with a
warning in the log (`Button modes: ...`). The folder is checked about once a second and reloaded
when its contents change.

On the first start of the interface the two examples `latch-nav` and `next-mode` are copied into a
missing `buttons/` folder (never overwriting anything). Without the interface:

    mkdir -p -m 0700 ~/.config/dofscroll/buttons
    cp /usr/share/dofscroll/examples/buttons/*.conf ~/.config/dofscroll/buttons/

## Using a button mode

The button action `buttonmode:<id>` in the usual `BUTTON_<n>` syntax means "behave like the action
in that file":

    [global]
    BUTTON_3 = buttonmode:latch-nav
    [app:resolve]
    BUTTON_3 = buttonmode:latch-nav
    BUTTON_5 = buttonmode:latch-nav       # the same button mode on a second button

It works wherever `BUTTON_<n>` works: in `[global]`, `[device:*]`, `[app:*]` and in joystick mode
files. **The precedence does not change**: a binding in the active mode file wins over `BUTTON_<n>`
of the resolved profile, and the profile order is global < device < mode overrides < app.

The daemon replaces the reference by the action of the file when it applies a profile; this
happens at runtime only. The configuration and the mode files keep the text `buttonmode:<id>`, and the
interface shows and saves it as it is.

An unknown id (no such file) is logged once (`unknown button mode '<id>'`) and the button does
nothing. In the interface such a reference is shown as `missing: <id>` and is not rewritten.
Deleting a button mode in the interface lists where it is still used first.

## In the interface

- **Profiles page:** a *Joystick mode* drop-down (what the axes do) and a drop-down per button. Its
  entries: *Default (inherit)* (device and program profiles; in the default profile *Nothing*), the
  button modes by name, then the plain actions (navigation, mode switch, toggles, and behind
  "Advanced" key and click). *Default (inherit)* removes the profile's own `BUTTON_<n>` line.
- **Button modes page:** list, an editor for name, description and action (the same action
  choices as on the Profiles page, without "button mode"), Save, Revert, New, Duplicate, Import,
  Delete and "Open button modes folder". The id never changes when you edit. A button mode can press
  a key or click: import only files you trust, the import dialog shows the loader warnings first.
