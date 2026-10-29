<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Joystick mode requests and contributions

New joystick modes and button modes are plain text files, so anybody can ask for one or send one. The list of what exists is on [Built-in joystick modes](Built-in-modes); the file format is on [Joystick modes](Modes).

## Ask for a joystick mode

Open an issue with the **Joystick mode proposal** form: https://github.com/HoZe-Lukas/dofScroll/issues/new/choose. Say whether it is a joystick mode or a button mode, the program or workflow it is for, what the channels (V, H, twist) or the button should do, and whether you tested it ("not tested" is fine). Proposals carry the label `mode idea`; the open ones are tracked under that label.

The former wishlist issue [#62](https://github.com/HoZe-Lukas/dofScroll/issues/62) is closed; every idea is now its own issue.

## Open joystick mode ideas

| Idea | Issue |
|---|---|
| `sheet`: spreadsheet cell navigation | [#86](https://github.com/HoZe-Lukas/dofScroll/issues/86) |
| `photo`: next/previous image, zoom | [#87](https://github.com/HoZe-Lukas/dofScroll/issues/87) |
| `zoom-pan`: zoom and horizontal pan for DAWs and NLEs | [#88](https://github.com/HoZe-Lukas/dofScroll/issues/88) |
| `vertical-only`: reading documents | [#89](https://github.com/HoZe-Lukas/dofScroll/issues/89) |
| `fast`: counterpart of `precision` (`precision` is already built in) | [#90](https://github.com/HoZe-Lukas/dofScroll/issues/90) |

You can comment on an idea, test a draft or pick it up and send the file.

## Send a joystick mode

Write the file, then open a pull request with the "new joystick mode" template: `https://github.com/HoZe-Lukas/dofScroll/compare/main...<your-branch>?template=new-mode.md`. Joystick modes go to `examples/modes/<id>.conf`, button modes to `examples/buttons/<id>.conf`. The id is the file name without `.conf` (`[a-z0-9_-]{1,32}`).

The checklist in short:

- the file is in `examples/modes/` or `examples/buttons/` and nothing else needs to be copied;
- SPDX header, `NAME` and a short one-line `DESCRIPTION`;
- the id is new and not reserved (`next`, `prev`, `normal`, `off`, `timeline`), and no built-in already does the same;
- the design principle below is respected;
- you read the file for `key:`, `keys:`, `zoom` and `mode:` actions, and chords or anything harmful are explained in `DESCRIPTION`;
- the built-in table in `docs/modes.md`, `docs/wiki/Modes.md` and the page [Built-in joystick modes](Built-in-modes) list the new joystick mode (a CI check fails when a built-in joystick mode is missing from that page);
- `cmake --build build --target check-fast` passes (it parses every embedded file without warnings and keeps ids unique);
- what you tested, or "not tested on hardware".

## Design principle

- **Joystick modes** put functions on the motions of the cap (the three channels, push and tilt separately, the lift), and the response should be analog: the effect follows the deflection (scroll speed, key repeat rate). Sensitivity overrides are fine.
- **A binding that is always the same** (a button that always sends Esc or a click) belongs in the vendor driver, not in dofscroll. dofscroll's button actions are for what depends on dofscroll's state: bindings that only apply in a joystick mode, joystick mode switching, latches and cycles, and anything that works together with the axis handling. Such proposals are declined or redirected.

## Safety

A joystick mode file can press any key while a button is held or an axis is deflected, including Ctrl, Alt, Super, function keys and chords such as `ctrl+w`. Keep keys to navigation where you can. If a chord is needed, name it in `DESCRIPTION` and in the pull request. The daemon warns about chords with `alt`, `super` or non-navigation keys. See the safety section of [Joystick modes](Modes) and [Security](Security).

## How a merged file becomes built in

The build embeds every file in `examples/modes/` and `examples/buttons/` into the program (`cmake/BuiltinLibrary.cmake`), so a merged file is part of the next release on every system, listed in the interface as "Built in". Users can still override it with a file of the same id in their own folder.
