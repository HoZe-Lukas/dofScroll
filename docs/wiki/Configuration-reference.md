<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Configuration reference

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

The source ([src/config.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/config.cpp)) is the reference; the full text is [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

## File and format

The file is UTF-8 (a BOM is accepted, never written; LF written, CRLF accepted); names may contain any Unicode text, matched with ASCII-only case folding. See docs/encoding.md.

| Item | Value |
|---|---|
| Config file | `~/.config/dofscroll/dofscroll.conf` (`$XDG_CONFIG_HOME` is used if it is an absolute path) |
| Joystick mode files | `~/.config/dofscroll/modes/*.conf`, see [Joystick modes](Modes) |
| Legacy paths | `~/.config/spnav-scroll/spnav-scroll.conf` and `~/.config/spnav-scroll.conf`, read only if the new file is missing |
| Windows | `%APPDATA%\dofscroll\dofscroll.conf`, same format (unverified on hardware) |

Format: `KEY = value`, sections `[name]`, comments with `#` or `;`. Keys are case-sensitive and upper case. The daemon reloads on `SIGHUP` and when the file changes; only `BACKEND` needs a restart. The tray rewrites the file without keeping comments (atomically, keeping the file mode).

Limits: file up to 1 MiB, line up to 8 KiB, 256 sections, 512 keys per section, 12 mix terms, 64 curve points. Duplicate key in one section: the last wins. Invalid values are rejected with a message in the log.

## Channels

Three channels, each with its own axis mix, sensitivity, dead zone, direction, curve and smoothing: `SCROLL_V` (vertical), `SCROLL_H` (horizontal) and `ZOOM` (twist).

| Key | Default V / H / ZOOM | Meaning |
|---|---|---|
| `<CH>_MIX` | `z + rx` / `x - rz` / `ry` | Sum of `[+-][weight*]axis`; axes `x y z rx ry rz`, e.g. `0.5*rx + z` |
| `<CH>_SENS` | `5` / `3` / `5` | Sensitivity 0 to 255; `0` switches the channel off |
| `<CH>_DEADZONE` | `30` / `20` / `40` | Dead zone in raw sensor units |
| `<CH>_SIGN` | `1` | `1` or `-1`: direction |
| `<CH>_CURVE` | `1.0` | Exponent 0.1 to 10, or a point list such as `0:0 50:0.1 200:1 350:3` (input after dead zone : factor, ascending, linear interpolation) |
| `<CH>_SMOOTH_MS` | `0` | Low-pass time constant in ms, 0 to 500; `0` is off |

The seeded config uses different values than these built-in defaults; it is a starting point. Older key names (`SCROLL_SENSITIVITY_X/Y`, `SCROLL_DEADZONE_X/Y`, `ZOOM_SENSITIVITY`, `INVERT_X/Y`, `ZOOM_INVERT`, `RESPONSE_CURVE`) are still accepted and mapped to the keys above.

## Zoom, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `INVERT_AXES` | `none` | Axes whose direction is flipped in every joystick mode: a list of `x y z rx ry rz`, or `none`; per device and per program as well |
| `ZOOM_MODIFIER` | `ctrl` | Keys held while a zoom action sends the mouse wheel: `ctrl`, `shift`, `alt`, `super` (Command on macOS), a combination such as `ctrl+shift`, or `none` |
| `ZOOM_LATERAL` | `x z rx rz` | Axes the twist must dominate to enter zoom |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor (at least 1.0) |
| `STEP_TRIGGER` | `20` | Level at which a `step:` channel action fires one key tap (steps per second after the channel's processing, 0.5 to 10000) |
| `BACKEND` | `auto` | `auto`, `x11` or `uinput`; `auto` picks uinput under Wayland, else X11; if the chosen backend fails the other is tried; needs a restart |
| `INPUT` | `auto` | `auto`, `driver` or `hid`. Windows and macOS only (ignored on Linux): read the SpaceMouse through the installed 3Dconnexion driver's interface or directly as raw HID; read from `[global]` only |
| `TICK_HZ` | `250` | Tick rate 30 to 1000 |
| `ENABLED` | `1` | `0` switches all output off and releases held keys |
| `MODE` | `normal` | Active joystick mode, see [Joystick modes](Modes) |
| `LIVE_FEED` | `1` | `0` disables the live data feed to the interface (axes view, axis assistant) |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED` | `0`, `0` | Deprecated (use `BUTTON_<n> = nav`; the interface migrates them): raw 0-based button index that turns X/Y into arrow keys while held |
| `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | `1`, `0` | Deprecated (use `BUTTON_<n> = click`; the interface migrates them): raw 0-based button index passed through as the left mouse button |
| `BUTTON_<n>` | none | Action of button `n` (`nav`, `toggle:nav`, `mode:<id>`, `hold:mode:<id>`, `toggle:mode:<id>`, `buttonmode:<id>`, ...), see [Buttons](Buttons) |

## Sections and precedence

| Section | Applies when |
|---|---|
| `[global]` | Always; base values |
| `[device:<text>]` | The device name reported by `spacenavd` contains `<text>` (case-insensitive substring) |
| `[app:<text>]` | The active window's class contains `<text>` (case-insensitive substring) |

Precedence, lowest to highest: `[global]` < matching `[device:*]` < overrides of the active joystick mode < matching `[app:*]`. See [Profiles](Profiles). `BACKEND` is read from `[global]` at start.

## Example files

All are validated by `cmake --build build --target check-config`. Packages install them in `/usr/share/dofscroll/examples/`.

| File | Content |
|---|---|
| [examples/default/linux/dofscroll.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/linux/dofscroll.conf) | Initial config seeded by the interface on Linux (`windows/` and `macos/` next to it, macOS experimental) |
| [examples/dofscroll-pro.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-pro.conf) | SpaceMouse Pro, buttons off |
| [examples/dofscroll-compact.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-compact.conf) | SpaceMouse Compact, both buttons active |
| [examples/dofscroll-multi-device.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-multi-device.conf) | Both device profiles in one file |
| [examples/modes/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/modes) | Built-in joystick modes (embedded in the program) |

## Text and encoding

All text is UTF-8. A leading BOM is accepted and never written; LF is written, CRLF is accepted. Names in `[app:*]` and `[device:*]` may contain any Unicode text (spaces, umlauts, CJK, emoji, `#` `;` `=` `[` `]`); only control characters are refused. Matching is case-insensitive for ASCII letters only, and text is not normalized (write macOS names in NFC). Invalid UTF-8 in a file gives a warning with `file:line`; the rest of the file is still used. See docs/encoding.md.

## Vendor driver settings

`spacenavd` applies its own sensitivity, dead zone, inversion and axis swap (`/etc/spnavrc`) before dofscroll sees any value. Keep it at the defaults and tune in dofscroll. If you changed it, adjust the mixes or run the axis assistant ("Axis assistant..." on the Axes page of the interface). Programs that read the SpaceMouse themselves (Blender, CAD) get input twice unless you switch dofscroll off for them with `ENABLED = 0` or `MODE = off` in an `[app:*]` section.
