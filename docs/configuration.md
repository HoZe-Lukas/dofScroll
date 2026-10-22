<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Configuration

The source (`src/config.cpp`, `src/config.hpp`) is the reference. This page describes the intended behavior.

## File and location

The file is UTF-8 (a BOM is accepted, never written; LF written, CRLF accepted); names may contain any Unicode text, see [encoding.md](encoding.md).

| Item | Value |
|---|---|
| Config file | `~/.config/dofscroll/dofscroll.conf` (`$XDG_CONFIG_HOME` is used if it is an absolute path) |
| Joystick mode files | `~/.config/dofscroll/modes/*.conf` (see [modes.md](modes.md)) |
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

Per-part keys for split channels: `SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`
(for example `SCROLL_V_TILT_SENS = 8`). A joystick mode can split the vertical or horizontal channel
into a push part (translation terms of the mix) and a tilt part (rotation terms) with different
actions, see [modes.md](modes.md#split-push-and-tilt); each part starts with the parent channel's
values and these keys replace single values with **absolute** values (not multipliers, same ranges as
the channel keys). They have no default, are not written to the config unless set, and act only while
the active joystick mode splits that channel.

Axes (libspnav convention, measured on a SpaceMouse Pro): push forward or back is `z`, sideways is
`x`, lift or press is `y`, tilt forward or back is `rx`, tilt sideways is `rz`, twist is `ry`. If you
changed `spacenavd` settings (axis swap, inversion), adjust the mixes or run the "Learn axes" wizard
in the tray.

## Zoom, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `INVERT_AXES` | `none` | Axes whose direction is flipped before anything else sees them, in every joystick mode: a list of `x y z rx ry rz` (or `none`). Use it when pushing scrolls the wrong way but tilting is right (`INVERT_AXES = z`). Like every key it can be set per device and per program; the Channels tab has a check box per motion. The sign of a channel (`*_SIGN`) flips the whole channel instead. |
| `ZOOM_MODIFIER` | `ctrl` | Keys held while a zoom action sends the mouse wheel: `ctrl`, `shift`, `alt`, `super` (the Command key on macOS; `cmd` is accepted), a combination such as `ctrl+shift`, or `none`. Set it per program or device where the zoom key differs, for example `super` on macOS. The joystick mode chips and cards show the keys of the profile they are shown for. |
| `ZOOM_LATERAL` | `x z rx rz` | Axes the twist must dominate to enter zoom. |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches. |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor (at least 1.0). |
| `STEP_TRIGGER` | `20` | Level at which a `step:` channel action (see [joystick modes](modes.md)) fires one key tap, measured in steps per second after the channel's dead zone, curve, smoothing and sensitivity (`deflection * SENS / 20`; 0.5 to 10000). The action re-arms at half of it. Raise it for a firmer push, lower it together with a low `*_SENS`. |
| `BACKEND` | `auto` | `auto`, `x11` or `uinput`. `auto` picks uinput under Wayland, else X11. If the chosen backend fails, the other is tried. Needs a daemon restart. |
| `INPUT` | `auto` | `auto`, `driver` or `hid`. **Windows and macOS only; ignored on Linux**, where spacenavd is the driver. How the SpaceMouse is read: `driver` through the interface of the installed official 3Dconnexion driver (loaded at run time from the installed driver, nothing bundled; see [legal-notes.md](legal-notes.md)), `hid` directly as raw HID, `auto` by a built-in table (macOS: driver first when installed, otherwise HID; Windows: HID first, the driver as the fallback when HID cannot be opened). With `driver` or `hid` exactly that path is used and nothing else is tried; an invalid value is reported and `auto` applies. Read from `[global]` only, applied on reload without a restart (switching takes a few seconds). The active path is shown as `input=driver` or `input=hid` in the status file and as "Input: 3Dconnexion driver" or "Input: direct (HID)" in the interface and in the log. The defaults of `auto` are unverified guesses until tested on hardware. |
| `TICK_HZ` | `250` | Tick rate 30 to 1000. Can differ per profile. |
| `ENABLED` | `1` | `0` switches all output off (scroll, zoom, keys, clicks) and releases held keys. |
| `MODE` | `normal` | Active joystick mode, see [modes.md](modes.md). Can be set in `[global]` (the joystick mode of everything that does not choose its own), `[device:*]` and `[app:*]`. |
| `LIVE_FEED` | `1` | `0` disables the live data feed to the UI (axes view, axis assistant); with `1` it only runs while a UI is connected. See `security.md`. |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED` | `0`, `0` | **Deprecated**, use `BUTTON_<n> = nav`. Button (0-based index) that turns X/Y into arrow keys while held. |
| `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | `1`, `0` | **Deprecated**, use `BUTTON_<n> = click`. Button (0-based index) that is passed through as the left mouse button. A click that is the same everywhere is better set in the vendor driver, see [modes.md](modes.md#buttons-and-the-vendor-driver). |
| `BUTTON_<n>` | none | Action of button `n`, same values as in joystick mode files, see [modes.md](modes.md). `buttonmode:<id>` uses a named button behavior from `buttons/<id>.conf`, see [button-modes.md](button-modes.md). |

### Deprecated: NAV_BUTTON and CLICK_BUTTON

`NAV_BUTTON` and `CLICK_BUTTON` (with `..._ENABLED`) are deprecated. They take the raw 0-based button
index as `spacenavd` reports it, and the log shows the index of every pressed button in `--debug`
mode. The daemon still understands them, so old files behave as before, but the interface does not
offer them any more: **all buttons are assigned with `BUTTON_<n>`** (1-based) using `nav`,
`toggle:nav`, `click`, `mode:<id>`, `mode:next`, `mode:prev`, `hold:mode:<id>`, `toggle:mode:<id>`
and `buttonmode:<id>`. `NAV_BUTTON = 2` with `NAV_BUTTON_ENABLED = 1` is `BUTTON_3 = nav`.

When `dofscroll-ui` loads a file that uses them it migrates them once, visibly: in the profile that
set them, `NAV_BUTTON` becomes `BUTTON_<n> = nav` and `CLICK_BUTTON` becomes `BUTTON_<n> = click`
(n = index + 1; a profile that only switches the inherited button off gets `BUTTON_<n> = none`), the
old keys are removed, the file is written atomically, the daemon reloads it and the footer says
"Moved the old navigation/click button settings to the Buttons tab". A key pair is moved all or
nothing: if the index is above the factory button count of the device or the profile already binds
that button to something else, the old keys stay in the file untouched and the Buttons tab lists
them as "Not moved". The count is that of the model named in a `[device:NAME]` section
(`factoryButtonCount()`, 32 for a model that is not in the table), so `NAV_BUTTON = 14`
(`BUTTON_15`) switched on in `[device:SpaceMouse Compact]` (2 buttons) is not moved, also when
the index is only inherited from `[global]`: the notice names the section, the key and the
button count, and you assign a button in the Buttons tab. `[global]` and `[app:*]` apply to
every device and use the count of the connected device (32 while it is unknown) instead of the
lowest count of the profiles in the file: a binding there is meant for the models that have
that button, and every device section is checked against its own count. A device section
that merely inherits an enabled button that it does not have is not an error. Saving never
adds keys the file did not have: `[global]` is written with exactly the keys it holds, so old
keys that stay in the file are kept as they are. Disabled keys (`..._ENABLED = 0`, which older saves wrote everywhere) are
dropped silently. The daemon and the config loader keep reading the old keys.

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. See [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

`BUTTON_<n>` in the main config works at runtime, in any section. Buttons are numbered from 1 and
at most 32. `n` must not exceed the factory button count of the device model
(`factoryButtonCount()`, not the count `spacenavd` reports). `BUTTON_0` is rejected.
The Qt interface learns the device name and this factory count from the daemon's live feed (device
info frame, see [port-architecture.md](port-architecture.md#live-feed)); without the feed the
Profiles page falls back to the `device=` line of the status file and the Axes page to the highest
button it has seen.
If the active joystick mode binds the same button, the joystick mode's binding wins over the config binding.

## Profiles

| Section | Applies when |
|---|---|
| `[global]` | Always. Base values and fallback. |
| `[device:<text>]` | The device name reported by `spacenavd` contains `<text>` (case-insensitive substring). |
| `[app:<text>]` | The active window's `WM_CLASS` contains `<text>` (case-insensitive substring). |

Sections contain only the keys they override. `BACKEND` is read from `[global]` at start, `INPUT` from `[global]` (an `INPUT` line in a profile section has no effect).

### Precedence

Lowest to highest: `[global]` < matching `[device:*]` < overrides of the active joystick mode < matching
`[app:*]`. All device sections are applied before all app sections, each in file order, so a later
matching section wins. The joystick mode is chosen by `MODE` from the resolved profile (global, device or
app), so an app section can select a joystick mode and still has the last word over the joystick mode's overrides.

### Inheritance

Everything can be set in `[global]` exactly like in any other section: the joystick mode (`MODE`),
every channel value and every button (`BUTTON_<n>`, with button modes). `[global]` is the base. A
`[device:*]` or `[app:*]` section holds only what it changes and inherits the rest, so the effective
value of a key is the first one found from the top of the precedence list above downwards: the
program section, the joystick mode's overrides, the device section, `[global]`, the built-in default.
Buttons are inherited per button: `BUTTON_2` of a profile replaces the `BUTTON_2` of `[global]`,
the other buttons stay; `BUTTON_<n> = none` in a profile switches an inherited button off for that
profile.

    [global]
    SCROLL_V_SENS = 5            # base for everything
    BUTTON_2 = nav               # every device and program: button 2 holds navigation

    [device:SpaceMouse Pro]
    SCROLL_V_SENS = 8            # this device: 8; every other key is inherited from [global]

    [app:firefox]
    BUTTON_2 = none              # in Firefox button 2 does nothing (nav is inherited, none overrides it)

The profile editor shows this per setting and per button ("Inherited from Global: 5", "Set in this
profile (inherited: 5)", "2 profiles override this"), see [ui.md](ui.md#inheritance). A program
profile cannot know which device is active at run time; the interface shows the value of
`[global]` and names the device profiles that set the same key.

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
`kdotool` (KDE), which the daemon looks for only in `/usr/bin`, `/usr/local/bin` and `/bin` (not in `PATH`,
so a `kdotool` in `~/.local/bin` is not used). Otherwise only `[global]` and `[device:*]` apply.

With two SpaceMice connected, `spacenavd` reports only one, so `[device:*]` applies to both. Where each device
arrives separately (Windows and macOS, direct HID) every device has its own profile and joystick mode; only one device per model
(a Pro next to a Compact, not two Compacts). See [multi-device.md](multi-device.md).

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
| `examples/modes/*.conf`, `examples/buttons/*.conf` | The built-in joystick modes and button modes (embedded into the program at build time, not installed or seeded). |

## Settings in the vendor driver

`spacenavd` (Linux) applies its own sensitivity, dead zone, inversion and axis swap (`/etc/spnavrc`)
before dofscroll sees any value. Keep it at the defaults and tune in dofscroll. Programs that read
the SpaceMouse directly (Blender, CAD) get input twice unless you switch dofscroll off for them
with `ENABLED = 0` or `MODE = off` in an `[app:*]` section. Restart `spacenavd` after editing
its configuration; the daemon reconnects by itself.
