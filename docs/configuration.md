<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Configuration

The source (`src/config.cpp`, `src/config.hpp`) is the reference. This page describes the intended behavior.

## File and location

| Item | Value |
|---|---|
| Config file | `~/.config/dofscroll/dofscroll.conf` (`$XDG_CONFIG_HOME` is used if it is an absolute path) |
| Mode files | `~/.config/dofscroll/modes/*.conf` (see [modes.md](modes.md)) |
| Legacy paths | `~/.config/spnav-scroll/spnav-scroll.conf`, `~/.config/spnav-scroll.conf` (read only if the new file is missing) |

The tray and the daemon read and write the same format. The daemon reloads on `SIGHUP` and when the
file changes, so you do not need to restart it. Only `BACKEND` needs a restart.

Format: `KEY = value`, sections `[name]`, comments with `#` or `;`. Keys are case-sensitive and
upper case. The easiest way to edit is the tray settings window. The tray rewrites the file
without keeping comments. It writes atomically, keeps the file mode and follows a symlink.

Limits: file up to 1 MiB, line up to 8 KiB, up to 256 sections, 512 keys per section, 12 mix
terms, 64 curve points. Duplicate keys in one section: the last wins. Invalid values are rejected
with a message in the log.

## Channels

Three channels, each with its own axis mix, sensitivity, dead zone, direction, curve and smoothing:
`SCROLL_V` (vertical), `SCROLL_H` (horizontal) and `ZOOM` (twist).

| Key | Default V / H / ZOOM | Meaning |
|---|---|---|
| `<CH>_MIX` | `z + rx` / `x - rz` / `ry` | Axis expression: sum of `[+-][weight*]axis`. Axes: `x y z rx ry rz`. Example: `0.5*rx + z`. |
| `<CH>_SENS` | `5` / `3` / `5` | Sensitivity 0 to 255. `0` switches the channel off. |
| `<CH>_DEADZONE` | `30` / `20` / `40` | Dead zone in raw sensor units. |
| `<CH>_SIGN` | `1` | `1` or `-1`: direction. |
| `<CH>_CURVE` | `1.0` | Exponent 0.1 to 10 (above 1: finer at light pressure) or a point list such as `0:0 50:0.1 200:1 350:3` (input after dead zone : factor; ascending; linear interpolation). |
| `<CH>_SMOOTH_MS` | `0` | Low-pass time constant in ms, 0 to 500. `0` = off. |

`<CH>` is `SCROLL_V`, `SCROLL_H` or `ZOOM`. Values in the seeded config
(`examples/default/dofscroll.conf`) differ from this table; the seeded file is a starting point.

Axes (libspnav convention, measured on a SpaceMouse Pro): push forward or back is `z`, sideways is
`x`, lift or press is `y`, tilt forward or back is `rx`, tilt sideways is `rz`, twist is `ry`. If you
changed `spacenavd` settings (axis swap, inversion), adjust the mixes or run the "Learn axes" wizard
in the tray.

## Zoom, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `ZOOM_LATERAL` | `x z rx rz` | Axes the twist must dominate to enter zoom. |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches. |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor (at least 1.0). |
| `BACKEND` | `auto` | `auto`, `x11` or `uinput`. `auto` picks uinput under Wayland, else X11. If the chosen backend fails, the other is tried. Needs a daemon restart. |
| `TICK_HZ` | `250` | Tick rate 30 to 1000. Can differ per profile. |
| `ENABLED` | `1` | `0` switches all output off (scroll, zoom, keys, clicks) and releases held keys. |
| `MODE` | `normal` | Active mode, see [modes.md](modes.md). |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED` | `0`, `0` | Button that turns X/Y into arrow keys while held. |
| `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | `1`, `0` | Older key: button that is passed through as the left mouse button. A click that is the same everywhere is better set in the vendor driver, see [modes.md](modes.md#buttons-and-the-vendor-driver). |
| `BUTTON_<n>` | none | Action of button `n`, same values as in mode files, see [modes.md](modes.md). |

`NAV_BUTTON` and `CLICK_BUTTON` are older keys. They take the raw button index as `spacenavd`
reports it, and the log shows the index of every pressed button in `--debug` mode. New setups
should use `BUTTON_<n>` with `nav`, `click` and the other actions.

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. See [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

`BUTTON_<n>` in the main config works at runtime, in any section. Buttons are numbered from 1 and
at most 32. `n` must not exceed the factory button count of the device model
(`factoryButtonCount()`, not the count `spacenavd` reports). `BUTTON_0` is rejected.
If the active mode binds the same button, the mode's binding wins over the config binding.

## Profiles

| Section | Applies when |
|---|---|
| `[global]` | Always. Base values and fallback. |
| `[device:<text>]` | The device name reported by `spacenavd` contains `<text>` (case-insensitive substring). |
| `[app:<text>]` | The active window's `WM_CLASS` contains `<text>` (case-insensitive substring). |

Sections contain only the keys they override. `BACKEND` is read from `[global]` at start.

### Precedence

Lowest to highest: `[global]` < matching `[device:*]` < overrides of the active mode < matching
`[app:*]`. All device sections are applied before all app sections, each in file order, so a later
matching section wins. The mode is chosen by `MODE` from the resolved profile (global, device or
app), so an app section can select a mode and still has the last word over the mode's overrides.

### Examples

    [global]
    SCROLL_V_SENS = 5

    [device:SpaceMouse Pro]
    SCROLL_V_SENS = 8

    [app:firefox]
    ZOOM_SENS = 12
    SCROLL_V_CURVE = 0:0 200:1 350:3

    [app:blender]
    ENABLED = 0                # 3D programs read the SpaceMouse themselves

    [app:resolve]
    MODE = timeline

Find the `WM_CLASS` of a window with `xprop WM_CLASS` and click the window. The daemon logs every
profile switch. Program profiles need an active-window query: always on X11, on Wayland only with
`kdotool` (KDE). Otherwise only `[global]` and `[device:*]` apply.

With two SpaceMice connected, `spacenavd` reports only one, so `[device:*]` applies to both.

## Example files

All are validated by `cmake --build build --target check-config`. Copy one to
`~/.config/dofscroll/dofscroll.conf` to use it. Packages install them in
`/usr/share/dofscroll/examples/`.

| File | Content |
|---|---|
| `examples/default/dofscroll.conf` | Initial config that the tray seeds on first start. |
| `examples/dofscroll-pro.conf` | SpaceMouse Pro, buttons off. |
| `examples/dofscroll-compact.conf` | SpaceMouse Compact, left button as nav button. |
| `examples/dofscroll-multi-device.conf` | Both device profiles in one file. |
| `examples/modes/*.conf` | Example modes, seeded into `~/.config/dofscroll/modes/`. |

## Settings in the vendor driver

`spacenavd` (Linux) applies its own sensitivity, dead zone, inversion and axis swap (`/etc/spnavrc`)
before dofscroll sees any value. Keep it at the defaults and tune in dofscroll. Programs that read
the SpaceMouse directly (Blender, CAD) get input twice unless you switch dofscroll off for them
with `ENABLED = 0` or `MODE = off` in an `[app:*]` section. Restart `spacenavd` after editing
its configuration; the daemon reconnects by itself.
