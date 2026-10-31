<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Configuration reference

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

The source ([src/config.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/config.cpp)) is the reference; the full text is [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

## File and format

The file is UTF-8 (a BOM is accepted, never written; LF written, CRLF accepted); names may contain any Unicode text, matched with ASCII-only case folding. See docs/encoding.md.

| Item | Value |
|---|---|
| Config file | `~/.config/dofscroll/dofscroll.conf`: global settings only (`$XDG_CONFIG_HOME` is used if it is an absolute path) |
| Profile files | `~/.config/dofscroll/devices/<name>.conf` (one `[device:NAME]` section per file) and `apps/<name>.conf` (one `[app:NAME]` section per file); the header names the device or program, the file name is only a label; every profile exists once |
| Joystick mode files | `~/.config/dofscroll/modes/*.conf`, see [Joystick modes](Modes) |
| Button mode files | `~/.config/dofscroll/buttons/*.conf`, see [Buttons](Buttons) |
| Backup of the migration | `~/.config/dofscroll/backup/dofscroll.conf.pre-split` (the old single file, kept by the upgrade; only if there was one) |
| Legacy paths | `~/.config/spnav-scroll/spnav-scroll.conf` and `~/.config/spnav-scroll.conf`, read only if the new file is missing |
| Windows | `%APPDATA%\dofscroll\` with `dofscroll.conf`, `devices\`, `apps\`, same format (unverified on hardware) |

Format: `KEY = value`, sections `[name]`, comments with `#` or `;`. Keys are case-sensitive and upper case. The daemon reloads on `SIGHUP` (the interface sends it through the unit, or through the process id in the status file when no unit runs the daemon) and when `dofscroll.conf` or a profile file changes; only `BACKEND` needs a restart. The settings window rewrites only the changed files, without keeping comments (atomically, keeping the file mode). Older versions kept the profiles as sections in `dofscroll.conf`; that format is still read (a profile file wins over a section of the same name), and the first start of the interface moves the sections into files and keeps the old file as `backup/dofscroll.conf.pre-split`. Downgrade: `cat devices/*.conf apps/*.conf >> dofscroll.conf` in the config folder. Details: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

Limits: `dofscroll.conf` and a profile file up to 1 MiB each, at most 255 profile files per folder, line up to 33 KiB, an action text up to 32 KiB, 256 sections, 10000 keys per section, 12 mix terms, 64 curve points. Duplicate key in one section: the last wins. Invalid values are rejected with a message in the log.

## Channels

Three channels, each with its own axis mix, sensitivity, dead zone, direction, curve and smoothing: `SCROLL_V` (vertical), `SCROLL_H` (horizontal) and `ZOOM` (twist). The push and tilt parts of V and H can be tuned on their own (`SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`, in joystick modes and in Global, device and program profiles; they act while the channel is split, the interface splits it on your first edit, and the keys of the whole channel are the starting values of both parts), and the lift is a sixth motion with `LIFT_MIX` (default `y`; the Windows and macOS seeds use `z`), `LIFT_SENS`, `LIFT_DEADZONE` (default 80), `LIFT_SIGN`, `LIFT_CURVE` and `LIFT_SMOOTH_MS`.

| Key | Default V / H / ZOOM | Meaning |
|---|---|---|
| `<CH>_MIX` | `z + rx` / `x - rz` / `ry` | Sum of `[+-][weight*]axis`; axes `x y z rx ry rz`, e.g. `0.5*rx + z` |
| `<CH>_SENS` | `5` / `3` / `5` | Sensitivity 0 to 255; `0` switches the channel off |
| `<CH>_DEADZONE` | `30` / `20` / `40` | Dead zone in raw sensor units |
| `<CH>_SIGN` | `1` | `1` or `-1`: direction |
| `<CH>_CURVE` | `1.0` | Exponent 0.1 to 10, or a point list such as `0:0 50:0.1 200:1 350:3` (input after dead zone : factor, ascending, linear interpolation) |
| `<CH>_SMOOTH_MS` | `0` | Low-pass time constant in ms, 0 to 500; `0` is off |

The seeded config uses different values than these built-in defaults; it is a starting point. Older key names (`SCROLL_SENSITIVITY_X/Y`, `SCROLL_DEADZONE_X/Y`, `ZOOM_SENSITIVITY`, `INVERT_X/Y`, `ZOOM_INVERT`, `RESPONSE_CURVE`) are still accepted and mapped to the keys above.

### Actions and keys per motion

| Key | Default | Meaning |
|---|---|---|
| `V_ACTION`, `H_ACTION`, `TWIST_ACTION`, `LIFT_ACTION` | none (the joystick mode decides; the lift: `none`) | What a channel does, in a profile or a joystick mode: `default`, `none`, `wheel_v`, `wheel_h`, `zoom`, `keys:<neg>,<pos>` (analog), `step:<neg>,<pos>` (digital, also `click`, `rightclick`, `middleclick`), `hold:<neg>,<pos>` |
| `V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION` | none | The same for a push or tilt part on its own |
| `MOUSE_SPEED` | `8` | Pixels per step for the actions `pointer_x` and `pointer_y` (0.1 to 200) |
| `<prefix>KEY_RATE_MIN`, `<prefix>KEY_RATE_MAX`, `<prefix>TRIGGER` | `0`, `0`, `STEP_TRIGGER` | Analog rate limits and trigger level per motion; prefix `SCROLL_V_PUSH_`, `SCROLL_V_TILT_`, `SCROLL_H_PUSH_`, `SCROLL_H_TILT_`, `ZOOM_` or `LIFT_` (see [Joystick modes](Modes)) |

## Zoom, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `INVERT_AXES` | `none` | Axes whose direction is flipped in every joystick mode: a list of `x y z rx ry rz`, or `none`; per device and per program as well. Applying the axis assistant sets it to `none` in `[global]` (profiles with their own value keep it) |
| `ZOOM_MODIFIER` | `ctrl` | Keys held while a zoom action sends the mouse wheel: `ctrl`, `shift`, `alt`, `super` (Command on macOS), a combination such as `ctrl+shift`, or `none` |
| `ZOOM_LATERAL` | `x z rx rz` | Axes the twist must dominate to enter zoom |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor (at least 1.0) |
| `STEP_TRIGGER` | `20` | Level at which a `step:` channel action fires one key tap (steps per second after the channel's processing, 0.5 to 10000) |
| `BACKEND` | `auto` | `auto`, `x11` or `uinput`; `auto` picks uinput under Wayland, else X11; if the chosen backend fails the other is tried; needs a restart |
| `INPUT` | `auto` | `auto`, `driver` or `hid`. Windows and macOS only (ignored on Linux): read the SpaceMouse through the installed 3Dconnexion driver's interface or directly as raw HID; read from `[global]` only |
| `TICK_HZ` | `250` | Tick rate 30 to 1000 |
| `ENABLED` | `1` | `0` switches all output off and releases held keys |
| `PRIORITY` | `0` | Only in device and program profile files: integer, order in which overlapping profiles of one kind are applied (ascending, the highest wins; ties by file name) |
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
| [examples/split/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/split) | The split layout: `dofscroll.conf` plus `devices/` and `apps/` with one profile per file |
| [examples/default/linux/dofscroll.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/linux/dofscroll.conf) | Initial config seeded by the interface on Linux (`windows/` and `macos/` next to it, macOS experimental) |
| [examples/dofscroll-pro.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-pro.conf) | SpaceMouse Pro, buttons off |
| [examples/dofscroll-compact.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-compact.conf) | SpaceMouse Compact, both buttons active |
| [examples/dofscroll-multi-device.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/dofscroll-multi-device.conf) | Both device profiles in one file |
| [examples/modes/](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/modes) | Built-in joystick modes (embedded in the program) |

## Text and encoding

All text is UTF-8. A leading BOM is accepted and never written; LF is written, CRLF is accepted. Names in `[app:*]` and `[device:*]` may contain any Unicode text (spaces, umlauts, CJK, emoji, `#` `;` `=` `[` `]`); only control characters are refused. Matching is case-insensitive for ASCII letters only, and text is not normalized (write macOS names in NFC). Invalid UTF-8 in a file gives a warning with `file:line`; the rest of the file is still used. See docs/encoding.md.

## Vendor driver settings

`spacenavd` applies its own sensitivity, dead zone, inversion and axis swap (`/etc/spnavrc`) before dofscroll sees any value. Keep it at the defaults and tune in dofscroll. If you changed it, adjust the mixes or run the axis assistant ("Axis assistant..." on the Axes page of the interface). Programs that read the SpaceMouse themselves (Blender, CAD) get input twice unless you switch dofscroll off for them with `ENABLED = 0` or `MODE = off` in an `[app:*]` section.
