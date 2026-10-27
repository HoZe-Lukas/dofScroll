<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Configuration

The source (`src/config.cpp`, `src/config.hpp`) is the reference. This page describes the intended behavior.

## File and location

Every file is UTF-8 (a BOM is accepted, never written; LF written, CRLF accepted); names may contain any Unicode text, see [encoding.md](encoding.md).

The configuration is a folder with one file per thing:

    ~/.config/dofscroll/
      dofscroll.conf              global settings only ([global])
      devices/<name>.conf         one device profile per file
      apps/<name>.conf            one program profile per file
      modes/  buttons/            joystick modes and button modes (see modes.md)
      backup/dofscroll.conf.pre-split   the old single file, kept by the upgrade (only if there was one)

| Item | Linux | Windows | macOS |
|---|---|---|---|
| Config folder | `~/.config/dofscroll/` (`$XDG_CONFIG_HOME` is used if it is an absolute path) | `%APPDATA%\dofscroll\` | `~/Library/Application Support/dofscroll/` |
| Global settings | `dofscroll.conf` in the config folder | same | same |
| Device and program profiles | `devices/` and `apps/` in the config folder | same | same |
| Joystick and button modes | `modes/`, `buttons/` | same | same |
| Legacy paths | `~/.config/spnav-scroll/spnav-scroll.conf`, `~/.config/spnav-scroll.conf` (read only if the new file is missing) | none | none |

The folder `devices/` of the configuration holds your device **profiles**. It is not the `devices/` folder
of the source tree, which holds the device **descriptions** (how a raw HID report maps to axes, see [devices.md](devices.md)).

The settings window and the daemon read and write the same format. The daemon only reads; the settings window
writes. The daemon reloads on `SIGHUP` and when `dofscroll.conf` or a file in `devices/`, `apps/` or `modes/`
changes (a change of one file reloads everything), so you do not need to restart it. Only `BACKEND` needs a restart.

Format: `KEY = value`, sections `[name]`, comments with `#` or `;`. Keys are case-sensitive and
upper case. The easiest way to edit is the settings window. The settings window rewrites a file
without keeping comments, and only the files whose profile you changed. It writes every file atomically
(temporary file, then rename), keeps the file mode and follows a symlink of `dofscroll.conf`.

Limits: file up to 1 MiB, line up to 8 KiB, up to 256 sections in `dofscroll.conf`, 512 keys per section, 12 mix
terms, 64 curve points. Duplicate keys in one section: the last wins. Invalid values are rejected
with a message in the log. The profile folders have limits too (number of files, size per file, total
number of profiles); a folder over the limit is read up to the limit and the log says so.

### Profile files

A profile file has the same syntax as always and exactly one section whose header carries the real name:

    # devices/spacemouse-pro.conf
    [device:SpaceMouse Pro]
    SCROLL_V_SENS = 8
    BUTTON_1 = nav

    # apps/firefox.conf
    [app:firefox]
    ZOOM_SENS = 12

- The **header is the truth**: the name in the header decides which device or program the file is for. The
  file name is only a label. If you rename a file by hand it is still read, and the settings window writes it
  under its own name the next time you change that profile.
- A file in `apps/` has an `[app:...]` header, a file in `devices/` a `[device:...]` header. A file with the
  wrong kind of header, with several sections, or with no section is defective (see "Errors per file").
- Every profile exists once. If two files have the same header name (for example a hand-made copy), the first
  one in the order of the file names wins; the second is ignored, with a warning in the log and in the
  profile list, and is never overwritten.
- A broken file only drops its own profile. All other profiles stay active.

The exported file of a profile (Profiles page, **Export...**) is a valid profile file. **Sharing or importing a
profile is copying a file**: copy it into `devices/` or `apps/` of the other computer (or use **Import...**
on the Profiles page) and the profile is there at the next reload.

### File names

The settings window names a file after the profile name (the "slug"):

- upper case ASCII letters become lower case (only ASCII; umlauts and other letters stay as they are);
- the characters `/ \ : * ? " < > |`, control characters and a dot or space at the start become `_`;
  a dot or space at the end becomes `_` too;
- the Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`) get a `_` appended;
- the name is at most 80 bytes (cut between two characters, never in the middle of one);
- when a name is cut, or two profiles would get the same file name (also names that differ only in
  upper and lower case, which Windows and macOS cannot keep apart), `-` and a short hash
  are appended. The hash is taken from the lower-cased name, so the file name is the same on every
  operating system and a copied configuration stays portable.

Examples: `SpaceMouse Pro` becomes `devices/spacemouse pro.conf`; `Q/A: notes` becomes `apps/q_a_ notes.conf`.
You can name a file anything you like when you create it by hand; only the header counts.

### Errors per file

The log (and the profile list in the settings window, which marks the profile "defective") names the file and
the reason when a profile file is not used: a line that is not valid (only that line is skipped, as in every
file), a wrong or missing header, more than one section, a second file with the same header name, a file that
is too big or not readable. The settings window never overwrites a defective file while the error stands: fix
or remove the file by hand, the profile is read again at the next reload.

### Hand-editing

Edit a profile file with any editor; the daemon reloads within about a second. If the settings window is
open and you changed a file it was about to write, it says so and reads your file instead of overwriting it.
To create a profile by hand, create a file in `devices/` or `apps/` with a header and the keys that differ from
`[global]`. To delete it, delete the file. Check a folder with `build/tests/check_config <config folder>`
(source build).

### The single file (legacy)

The old format stays readable: `[device:...]` and `[app:...]` sections inside `dofscroll.conf` are still
accepted, for example in a hand-written file or in a dotfile repository with a symlinked `dofscroll.conf`.
When the same profile also has a file in `devices/` or `apps/`, **the file wins** and the section is ignored with
a warning. The first start of the new interface moves the profiles of an existing `dofscroll.conf` into their
own files, see [installation.md](installation.md#upgrading-the-configuration-one-file-per-profile). A
`dofscroll.conf` that is a symlink is not split: it is only read, and the settings window keeps writing the single file.

### Downgrade

An older version reads only `dofscroll.conf`, so after the split it sees `[global]` and no profiles (nothing is
lost). To go back, in the config folder:

    cat devices/*.conf apps/*.conf >> dofscroll.conf

or restore the old file: `cp backup/dofscroll.conf.pre-split dofscroll.conf` (the backup is the file as it was
before the split, without changes you made after it). There is no button for this.

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
(`examples/default/<os>/dofscroll.conf`) differ from this table; the seeded file is a starting point. The axis order of the official driver differs between the systems, so Linux, Windows and macOS each have their own file: Linux uses mixes of two axes (`z + rx`, `x - rz`, `ry`), Windows mixes of push and tilt as well (`y + rx`, `x - ry`, `-rz`: scrolling by pushing and by tilting, measured with the official driver). The macOS file is experimental: it carries the Windows values (the 3DxWare client API delivers the axes in the same order as on Windows, read from the code, not verified on a Mac), run the axis assistant there.

Per-part keys for split channels: `SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS>`
(for example `SCROLL_V_TILT_SENS = 8`). A joystick mode can split the vertical or horizontal channel
into a push part (translation terms of the mix) and a tilt part (rotation terms) with different
actions, see [modes.md](modes.md#split-push-and-tilt); each part starts with the parent channel's
values and these keys replace single values with **absolute** values (not multipliers, same ranges as
the channel keys). They have no default, are not written to the config unless set, and act only while
the active joystick mode splits that channel.

Axes (libspnav convention, measured on a SpaceMouse Pro): push forward or back is `z`, sideways is
`x`, lift or press is `y`, tilt forward or back is `rx`, tilt sideways is `rz`, twist is `ry`. If you
changed `spacenavd` settings (axis swap, inversion), adjust the mixes or run the axis assistant
(Axes page of the interface, "Axis assistant...").

With the official 3Dconnexion driver on Windows (and the 3DxWare client API on macOS) the axes are named
differently: push forward or back is `y`, sideways is `x`, lift or press is `z`, tilt forward or back
is `rx`, tilt sideways is `ry`, twist is `rz` (the default uses `y + rx`, `x - ry` and `-rz`). The
interface does not assume either order: it reads what your mixes say (see "Where the interface gets the
axis meaning" in [ui.md](ui.md)). The file format is the same everywhere; a profile copied between
systems needs its mixes adjusted.

## Zoom, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `INVERT_AXES` | `none` | Axes whose direction is flipped before anything else sees them, in every joystick mode: a list of `x y z rx ry rz` (or `none`). Use it when pushing scrolls the wrong way but tilting is right (`INVERT_AXES = z`). Like every key it can be set per device and per program; the Channels tab has a check box per motion. The sign of a channel (`*_SIGN`) flips the whole channel instead. |
| `ZOOM_MODIFIER` | `ctrl` | Keys held while a zoom action sends the mouse wheel: `ctrl`, `shift`, `alt`, `super` (the Command key on macOS; `cmd` is accepted), a combination such as `ctrl+shift`, or `none`. Set it per program or device where the zoom key differs, for example `super` on macOS. `none` sends the plain wheel, so a twist scrolls instead of zooming. It affects only the mouse-wheel zoom action, not zoom done with `keys:`. The joystick mode chips and cards show the keys of the profile they are shown for. |
| `ZOOM_LATERAL` | `x z rx rz` | Axes the twist must dominate to enter zoom. An axis that is part of `ZOOM_MIX` itself is ignored here. The axis assistant sets this list to the axes of the two scroll channels. |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches. |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor (at least 1.0). |
| `STEP_TRIGGER` | `20` | Level at which a `step:` channel action (see [joystick modes](modes.md)) fires one key tap, measured in steps per second after the channel's dead zone, curve, smoothing and sensitivity (`deflection * SENS / 20`; 0.5 to 10000). The action re-arms at half of it. Raise it for a firmer push, lower it together with a low `*_SENS`. |
| `BACKEND` | `auto` | `auto`, `x11` or `uinput`. `auto` picks uinput under Wayland, else X11. If the chosen backend fails, the other is tried. Needs a daemon restart. |
| `INPUT` | `auto` | `auto`, `driver` or `hid`. **Windows and macOS only; ignored on Linux**, where spacenavd is the driver. How the SpaceMouse is read: `driver` through the interface of the installed official 3Dconnexion driver (loaded at run time from the installed driver, nothing bundled; see [legal-notes.md](legal-notes.md)), `hid` directly as raw HID, `auto` by a built-in table (macOS: driver first when installed, otherwise HID; Windows: HID first, the driver as the fallback when HID cannot be opened). With `driver` or `hid` exactly that path is used and nothing else is tried; an invalid value is reported and `auto` applies. Read from `[global]` only, applied on reload without a restart (switching takes a few seconds). The active path is shown as `input=driver` or `input=hid` in the status file and as "Input: 3Dconnexion driver" or "Input: direct (HID)" in the interface and in the log. The defaults of `auto` are unverified guesses until tested on hardware. |
| `TICK_HZ` | `250` | Tick rate 30 to 1000. Can differ per profile. |
| `ENABLED` | `1` | `0` switches all output off (scroll, zoom, keys, clicks) and releases held keys. |
| `PRIORITY` | `0` | Only in `[device:*]` and `[app:*]` profiles (split layout): integer, ascending order in which overlapping profiles of one kind are applied, the highest wins; ties by file name. See "Order of overlapping profiles". |
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
"Moved the old navigation/click button settings to the Motions and buttons tab". A key pair is moved all or
nothing: if the index is above the factory button count of the device or the profile already binds
that button to something else, the old keys stay in the file untouched and the Motions and buttons tab lists
them as "Not moved". The count is that of the model named in a `[device:NAME]` section
(`factoryButtonCount()`, 32 for a model that is not in the table), so `NAV_BUTTON = 14`
(`BUTTON_15`) switched on in `[device:SpaceMouse Compact]` (2 buttons) is not moved, also when
the index is only inherited from `[global]`: the notice names the section, the key and the
button count, and you assign a button on the Motions and buttons tab. `[global]` and `[app:*]` apply to
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

| Section (file) | Applies when |
|---|---|
| `[global]` (`dofscroll.conf`) | Always. Base values and fallback. |
| `[device:<text>]` (`devices/<name>.conf`) | The device name reported by `spacenavd` contains `<text>` (case-insensitive substring). |
| `[app:<text>]` (`apps/<name>.conf`) | The active window's `WM_CLASS` contains `<text>` (case-insensitive substring). |

Below, "section" means the section of a profile, whether it is in its own file or (legacy) in `dofscroll.conf`.
A profile holds only the keys it overrides. `BACKEND` is read from `[global]` at start, `INPUT` from `[global]` (an `INPUT` line in a profile section has no effect).

### Precedence

Lowest to highest: `[global]` < matching `[device:*]` < overrides of the active joystick mode < matching
`[app:*]`. All device profiles are applied before all app profiles, each in the order of their `PRIORITY` (see
"Order of overlapping profiles" below), so a later matching profile wins. The joystick mode is chosen by `MODE` from the resolved profile (global, device or
app), so an app section can select a joystick mode and still has the last word over the joystick mode's overrides.

### Order of overlapping profiles

Matching is by substring, so several profiles of one kind can match at once (`[app:fire]` and `[app:firefox]` both
match the window class `firefox`). They are all applied, device profiles before program profiles, and the
later one wins for every key both set. The order inside one kind is:

- **Single file (legacy):** the order of the sections in `dofscroll.conf`.
- **Split layout:** the optional key `PRIORITY` in the profile, an integer, default 0, ascending (the profile with the
  highest number is applied last and wins). Profiles with the same `PRIORITY` are applied in the order of their file
  names. `PRIORITY` is read only from the profile itself; a `PRIORITY` line in `[global]` has no effect. It exists because
  files have no order of their own and a file name is a poor place for it.

Example: both match Firefox; `fire` sets the zoom for all programs whose name contains "fire", `firefox` refines it.

    # apps/fire.conf
    [app:fire]
    PRIORITY = 10
    ZOOM_SENS = 8
    SCROLL_V_SENS = 4

    # apps/firefox.conf
    [app:firefox]
    PRIORITY = 20            # applied after "fire": ZOOM_SENS 12 wins, SCROLL_V_SENS 4 is inherited from it
    ZOOM_SENS = 12

The upgrade keeps your old behavior: when a kind has more than one profile, the migration gives them `PRIORITY` 10, 20,
30 ... in the order they had in the old file, so the split layout resolves exactly like the old file. A kind with one
profile gets no `PRIORITY` line. To change the order by hand, change the numbers (leave gaps such as 10, 20, 30 so you
can insert one later). The settings window shows the order in the profile list and offers "Move up" and "Move down",
which renumber the `PRIORITY` lines for you.

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

All are validated by `cmake --build build --target check-config`. Copy a single file to
`~/.config/dofscroll/dofscroll.conf` to use it (the legacy single-file format), or copy the folder
`examples/split/` into `~/.config/dofscroll/` for one file per profile. Packages install them in
`/usr/share/dofscroll/examples/`.

| File | Content |
|---|---|
| `examples/default/linux/`, `windows/`, `macos/` (`dofscroll.conf`) | Initial config that the interface seeds on first start, one per operating system (macOS: experimental, untested). It has no device profile: the first attached device gets its own profile once (button 1 = navigation). |
| `examples/dofscroll-pro.conf` | SpaceMouse Pro, buttons off. |
| `examples/dofscroll-compact.conf` | SpaceMouse Compact, left button as nav button. |
| `examples/dofscroll-multi-device.conf` | Both device profiles in one file (legacy single-file format). |
| `examples/split/` | The split layout: `dofscroll.conf` with `[global]`, `devices/` and `apps/` with one profile per file. |
| `examples/modes/*.conf`, `examples/buttons/*.conf` | The built-in joystick modes and button modes (embedded into the program at build time, not installed or seeded). |

## Settings in the vendor driver

`spacenavd` (Linux) applies its own sensitivity, dead zone, inversion and axis swap (`/etc/spnavrc`)
before dofscroll sees any value. Keep it at the defaults and tune in dofscroll. Programs that read
the SpaceMouse directly (Blender, CAD) get input twice unless you switch dofscroll off for them
with `ENABLED = 0` or `MODE = off` in an `[app:*]` section. Restart `spacenavd` after editing
its configuration; the daemon reconnects by itself.
