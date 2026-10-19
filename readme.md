# dofscroll – Technical Specification & Documentation

dofscroll turns a 3Dconnexion SpaceMouse into smooth scrolling, zooming, arrow keys and mouse buttons on Linux (X11 and, via uinput, Wayland). This document is the technical context for development, operation, moving to a new system and further development. It was written against Red Hat Enterprise Linux 9 (RHEL 9).

The source code (`src/`) is the single source of truth for *behavior*. This document deliberately contains no code excerpts, only architecture, contracts (config keys, file paths) and operating instructions, so that it cannot drift away from the implementation.

---

## 1. Overview

Two separate processes, one shared config format:

| Process | Source | Task |
|---|---|---|
| `dofscroll` | `src/dofscroll.cpp` (+ `config`, `dsp`, `modes`, `output*`, `activewin`) | Reads the SpaceMouse via `spacenavd`/`libspnav` and generates scroll/zoom/arrow-key/mouse-button events via X11/XTest **or** uinput (Wayland). Time-critical, runs permanently in the background. |
| `dofscroll-tray` | `src/dofscroll_tray.cpp` | Tray icon (AppIndicator/StatusNotifierItem) with a settings window (GTK3). Writes `~/.config/dofscroll/dofscroll.conf` and triggers a live reload in the daemon via SIGHUP. Seeds example configuration on first start (see section 4). |

System architecture:

- Operating system: RHEL 9 (tested on release 9.6).
- Hardware driver: `spacenavd` (system service, from EPEL) for low-level hardware communication and device state.
- User-space event processor: `dofscroll`, attached to `spacenavd` via `libspnav`.
- Event injection: X11/XTest (`XTestFakeButtonEvent`/`XTestFakeKeyEvent`) or uinput for mouse scroll (buttons 4/5/6/7), zoom (Ctrl+scroll), keys and mouse buttons.
- Architectural benefit: the system driver (`spacenavd`, root, system-wide) is separated from the user-space translator (`dofscroll`, a `--user` service), which allows per-user fine tuning without root privileges for the logic.
- Windows and macOS are planned (see section 9); only Linux is implemented today.

---

## 2. `src/dofscroll.cpp` – scroll/zoom daemon

Module layout (the headers in `src/*.hpp` are the contract between the parts):

| File | Task |
|---|---|
| `config.*` | File format, `Settings`, profiles (`[device:*]`, `[app:*]`), resolution – used by daemon **and** tray |
| `dsp.*` | Mix → sign → smoothing → dead zone → curve → speed; step accumulator; zoom detection (unit-tested) |
| `modes.*`, `keys.hpp` | Mode plugins (declarative files, see section 4) and the named keys they may press |
| `device_db.hpp` | Factory button count per device model (see "Button count") |
| `seed.*` | First-start seeding of example configuration (called by the tray) |
| `output.*`, `output_x11.cpp`, `output_uinput.cpp` | Event output: XTest or uinput; `BACKEND = auto` selects uinput under Wayland, otherwise X11 |
| `activewin.*` | Active program (`WM_CLASS`) for `[app:*]` profiles; under KDE/Wayland optionally via `kdotool` |
| `dofscroll.cpp` | Reader thread + tick loop, profile switching, signals, `--debug` |

Architecture (real-time capable, decoupled from the driver's event rate):

- A **reader thread** blocks on `spnav_wait_event` and only writes the most recent raw axis/button values into lock-free `std::atomic` variables. It sends no events itself.
- The **main thread** runs at a fixed tick rate (`TICK_HZ`, default 250 Hz, `std::chrono::steady_clock`) independent of incoming events and reads the current state every time. Scroll steps are therefore spread evenly over time instead of being sent in bursts, which fixes jerky scrolling.
- Accumulation is **time-based** (`* dt`), not per event, so the scroll speed stays proportional to the deflection (linearly, or non-linearly via `*_CURVE`).
- A floating-point accumulator (double, threshold 1.0) makes sure no fractions are lost even at low sensitivities.
- The process reacts to **SIGHUP** and reloads the config in the main thread (no restart needed) – used by the tray via `systemctl --user kill -s HUP dofscroll.service`.

### Modes, nav button and click button

What the three channels (vertical, horizontal, twist) and the device buttons do is decided by the **active mode** (section 4, "Modes"). The default mode `normal` keeps the classic behavior:

- **Nav button**: while `NAV_BUTTON` is held, X/Y deflections are sent as arrow keys (up/down/left/right) instead of scroll events; zoom is disabled meanwhile.
- **Click button**: passed through 1:1 (press + release) as the left mouse button, including click-and-hold for dragging.
- Both have their own enable switch (`NAV_BUTTON_ENABLED`, `CLICK_BUTTON_ENABLED`), independent of the configured button index – so the function can be switched off completely instead of merely choosing an unused button index.

Modes can additionally bind any button to a mouse button or a named key (`BUTTON_<n>`).

### Zoom (twist)

Twisting the cap (`ry`) zooms via Ctrl+scroll. Axis assignment (confirmed on a SpaceMouse Pro with a measurement tool, libspnav convention x right / y up / z towards the user): vertical scroll = `z + rx` (push or tilt forward/back), horizontal = `x - rz` (push or tilt sideways), `y` (lift/press) is ignored because it strongly co-varies with every push. Ctrl is held while zooming (not pressed per step). Zoom mode latches: it is entered when the twist has been stronger than every other single axis (x/z/rx/rz, maximum instead of sum) for ≥ ~40 ms in a row; afterwards it stays active until the twist falls below `ZOOM_DEADZONE` or another axis exceeds it by a factor of 1.5. On SIGTERM/SIGINT, Ctrl and the click button are released. `dt` is limited to 50 ms (no scroll burst after suspend). Diagnostics: stop the service and run `dofscroll --debug` in a terminal – it prints h/v/twist/lateral ~10 times per second together with the zoom decision.

### Button count

The number of configurable buttons (`BUTTON_<n>`, tray UI, validation in mode files) follows the **factory button count of the device model**, not the count the device or `spacenavd` reports (`spnav_dev_buttons()`). The mapping from device name to factory count lives in `src/device_db.hpp` (`factoryButtonCount(deviceName, reported)`); for unknown devices the reported count is used as a fallback. Everything that needs "how many buttons" must go through this function, never the reported number alone.

### SpaceMouse Compact vs. Pro

Both deliver the same 6 axes, so the default mixes fit both; the buttons differ (Compact 2, Pro 15). On connect the daemon logs the device name and button count, warns about non-existing `NAV_BUTTON`/`CLICK_BUTTON` (only if the respective `*_ENABLED` option is on) and logs every button press with its index in `--debug` mode (`journalctl --user -u dofscroll.service -f`) – this is how you find the right number.

Assignment in the Compact profile (`examples/dofscroll-compact.conf`):

| Button | Index | Effect |
|---|---|---|
| left | `NAV_BUTTON = 0` | Hold: arrow keys instead of scrolling (nav mode); release: normal scrolling again. |
| right | `CLICK_BUTTON = 1` | Passed through 1:1 as the left mouse button (click, hold, drag). |

Both are active there via `NAV_BUTTON_ENABLED = 1` / `CLICK_BUTTON_ENABLED = 1`; on the Pro they stay off. The assignment left = 0 / right = 1 is an assumption to be confirmed on hardware – if the buttons are swapped, swap the two numbers.

### Connection resilience (`spacenavd` not needed at start, hot-plug safe)

- `dofscroll` relies entirely on `spacenavd` for hardware access – it neither can nor should replace it. What it does: **never crash hard** just because `spacenavd` is not (yet, or no longer) reachable.
- **Start without a running `spacenavd` / without a connected SpaceMouse:** `initialize()` only opens the X11 display; the connection to `spacenavd` is made by the reader thread via `connectWithRetry()` with exponential backoff (0.5 s → 1 s → 2 s → 4 s, capped at 5 s). The systemd service therefore becomes `active (running)` immediately instead of repeatedly terminating with `status=1/FAILURE` and hitting `StartLimitBurst`.
- **Connection loss to `spacenavd`** (daemon restarted/crashed): `spnav_wait_event` returns `0`, the reader thread closes the connection, resets the deflection state to 0 immediately and calls `connectWithRetry()` again – fully automatic, no process restart needed.
- **Physically plugging/unplugging the SpaceMouse** is `spacenavd`'s job (hotplug via udev/`AF_NETLINK`) – `dofscroll`'s client socket connection is unaffected; there are simply no/again motion events.
- Additional safety net: a **freshness watchdog** (`last_motion_ns`, `STALE_NS` = 250 ms) treats the deflection as 0 as soon as no new motion event has arrived for more than 250 ms. This prevents "stuck" endless scrolling with an unplugged cable, a `spacenavd` crash or a single lost event.
- **Backoff across connection losses:** the wait counter (0.5 s → 5 s) is kept between connection attempts and only reset when the connection delivered events or lasted ≥ 10 s – a connection that closes immediately therefore does not cause a loop without pause.
- **Window query (`[app:*]`) decoupled:** X11 round trips and `kdotool` run in a background thread (every 100 ms, `kdotool` 250 ms with a 200 ms limit); the tick thread only reads the cache and can therefore never block (no stuck Ctrl). `main` calls `XInitThreads()` for this.
- **Device removed:** when `spacenavd` reports `SPNAV_DEV_RM`, axes and button mask are cleared (a held click button is released). Deliberately no timeout: a held button with a resting puck generates no events and must not be released.
- **Stuck keys after kill/crash:** at start the daemon releases Ctrl, arrow keys and the left mouse button (`forceReleaseAll`), and the unit runs `dofscroll --release-keys` as `ExecStopPost` (opens only the output, releases everything, exits).
- **Config reload:** a reload without actually changed values (file merely touched, identical content, failed load) leaves held keys untouched; if the profile changes, only Ctrl is released, a held click stays held. On a mode change, held mode keys and buttons are released.
- All connection events go to `stderr`, under systemd automatically into the journal: `journalctl --user -u dofscroll.service`.
- **Status and log file (for the tray):** the daemon writes `dofscroll-status` into `$XDG_RUNTIME_DIR` (`key=value`: `profile`, `device`, `app`, `enabled`, `paused`, `devices_seen`, `updated`; replaced atomically on change, at least once per second for `updated`; removed on clean exit) and `dofscroll.log` (copy of the log lines with timestamp, rolling: at most 300 lines, after that only the newest 200 remain; in `--debug` mode up to 2000 lines). Without `XDG_RUNTIME_DIR` or on write errors nothing happens (never fatal). Format: `src/common.hpp`, permissions: `docs/security.md`.
- **Multiple SpaceMice:** with two connected devices `spacenavd` reports only one (measured: Pro + Compact → only "SpaceMouse Pro"); `[device:*]` then applies to both, and the buttons of both devices share their indices. The daemon counts connected 3Dconnexion devices from `/proc/bus/input/devices` (after every (re)connect and about every 10 s, `src/device_count.*`), logs a hint once per change when there are ≥ 2 and writes the number as `devices_seen` into the status file.

---

## 3. `src/dofscroll_tray.cpp` – tray icon & settings window

Deliberately a **separate process**, so that the GTK main loop and tray overhead never affect the daemon's time-critical scroll tick thread. Technology: GTK3 + libappindicator3 (works natively under KDE Plasma via StatusNotifierItem; under GNOME the AppIndicator extension may be needed).

- Right-click menu on the tray icon: "Settings…" opens a window with sliders/spin boxes for all config values (sensitivities, dead zones, invert, response curve, nav button, click button, each with an enable checkbox), "Open configuration file" (the editor is started via `systemd-run --user` outside the tray sandbox), "Restart daemon" and "Quit" (closes only the tray app). At the bottom there is a list of **program modes** (programs and devices → mode), generated dynamically from the loaded mode files.
- Every change is written (debounced, 250 ms after the last input) to `~/.config/dofscroll/dofscroll.conf`, and the running `dofscroll` service is triggered to reload live via `systemctl --user kill -s HUP dofscroll.service` – no restart, no scroll interruption.
- The tray app does not duplicate config logic as a second source of truth – it reads/writes exactly the same file format as the daemon.
- Own icon (`icons/dofscroll-tray.svg`): a dark puck (top view of the SpaceMouse cap) with a light 4-way arrow in the middle – symbolizing that deflection in any direction becomes scroll/pan. The accent dot in the middle stands for the neutral position.
  - The icon folder is searched at runtime: `icons/` next to the resolved binary (repo), then `~/.local/share/dofscroll-tray/icons`, then `/usr/local/share/dofscroll-tray/icons`. A dead symlink under `/usr/local` is therefore no problem. The window icon is set via `gtk_window_set_default_icon_from_file()` (`_NET_WM_ICON`); the design is a light puck with a dark border/arrow so that it stays recognizable in dark and light mode.
  - For the tray itself it is loaded via `app_indicator_set_icon_theme_path()`/`app_indicator_set_icon_full()`, with `input-mouse` as fallback name if the custom icon is missing.
  - For the settings window the same icon theme path is additionally registered via `gtk_icon_theme_append_search_path()` and set as the default window icon with `gtk_window_set_default_icon_name()` – without it the window would show the generic GTK logo in the task bar although the tray already uses the custom icon.
- **UI localization:** all UI strings are English in the source and translated through gettext (see "Localization" below).

The settings window has two tabs:

- **Channels:** profile selection and all values per channel (mix, sensitivity, dead zone, direction, curve with preview, smoothing), backend, tick rate, buttons. The zoom detection (`ZOOM_LATERAL`, `ZOOM_ENTER_MS`, `ZOOM_EXIT_RATIO`) has no sliders and is maintained in the file.
- **Axes:** (As long as this tab is open in the **active** tray window or the wizard is running, the daemon pauses its output – no scrolling, zooming, arrow keys or clicks – so that the movements are only measured. Mechanism: the tray touches the file `$XDG_RUNTIME_DIR/dofscroll-suspend` about every 150 ms, the daemon checks it ~20 times per second and pauses while it is younger than 600 ms; switching tabs or windows or closing removes it immediately, a crashed tray lifts the pause after 600 ms at the latest. Without `XDG_RUNTIME_DIR` there is no pause. The daemon logs "output paused/active again".) A sketch of the cap with the axes `x`, `y`, `z`, `rx`, `ry`, `rz` (the currently selected channel is highlighted), live bars for all six axes (the tray opens its own reading libspnav connection for this, only while the window is open) and the **"Learn axes"** wizard: you perform five movements (about 3 s of measuring each; the two tilt steps are optional and can be skipped; pull towards you, tilt forward/back, push sideways, tilt sideways, twist), the wizard picks the axis with the largest magnitude (including sign) for each movement and enters `SCROLL_V_MIX`, `SCROLL_H_MIX` and `ZOOM_MIX` in the selected profile. The evaluation lives in `src/axis_wizard.hpp` (unit-tested). The device does not report which number physically corresponds to which movement – that is why it is measured.

### Localization

UI strings (tray, settings window, log window) are English in the source code (the source language) and translated through gettext; daemon log lines stay English. Translations live in `po/*.po` (the only place in the repository where non-English text is allowed); German is included. The active language follows the usual locale environment (`LC_MESSAGES`/`LANGUAGE`); without a matching catalog the English source strings are shown.

To add a language: see `CONTRIBUTING.md` (create `po/<lang>.po` from the translation template in `po/`, translate the entries, rebuild).

---

## 4. Configuration file (`~/.config/dofscroll/dofscroll.conf`)

Lives in its own directory `~/.config/dofscroll/` (mode `0700`; the tray unit may only write to this directory, see `docs/security.md`). As long as this file is missing, `defaultConfigPath()` falls back to the old paths `~/.config/spnav-scroll/spnav-scroll.conf` and `~/.config/spnav-scroll.conf` (see "Migration" below). It is read/written by **both** processes through the same library (`src/config.cpp`) and is **not** part of this repository (purely user-specific). If the file is missing, both start with defaults. Old flat files (`SCROLL_SENSITIVITY_Y` etc.) remain readable; on the first save through the tray they are converted to the new names (comments are lost when saving).

### First-start seeding of examples

When the tray starts and no configuration exists yet, it seeds the configuration folder from the examples shipped with the repository (`src/seed.*`): `examples/default/dofscroll.conf` becomes the initial config and `examples/modes/*.conf` become the initial mode files in `~/.config/dofscroll/modes/`. Seeding never overwrites an existing file. The daemon has no write access to the configuration folder, so seeding is done by the tray only; the daemon picks up the new files through its automatic reload.

### Migration from the time before `dofscroll` (project name `spnav-scroll`)

The project was formerly called `spnav-scroll`; the config first lived directly at `~/.config/spnav-scroll.conf`, later at `~/.config/spnav-scroll/spnav-scroll.conf`. The current path `~/.config/dofscroll/dofscroll.conf` allows the tray unit to write only to its own directory instead of the whole `~/.config`. Nothing has to be done manually (the more recent old location takes precedence over the oldest):

- The **tray** takes over the old file at start: it copies it to the new path (directory `0700`). If the old file is a symlink (e.g. into a dotfile repo), the symlink is recreated in the new directory instead of copying the content.
- The **legacy file stays untouched**. After a check (look at the settings in the tray) it can be deleted: `rm -r ~/.config/spnav-scroll ~/.config/spnav-scroll.conf`.
- Until then the **daemon** reads the legacy path as a fallback and follows the switch to the new path within about 1 s, without a restart. It therefore works unchanged even without a running tray (or if the migration fails).
- On the first start after the update the tray unit must be reloaded (`systemctl --user daemon-reload && systemctl --user restart dofscroll-tray.service`) so that the changed sandbox directives (`ConfigurationDirectory=`, `ReadWritePaths=%E/dofscroll %t`) apply; otherwise the old process cannot write to the new directory.

### Channels and axes

Three channels, each with its own axis mix, sensitivity, dead zone, direction, curve and smoothing: `SCROLL_V` (vertical), `SCROLL_H` (horizontal), `ZOOM`.

| Key (per channel `SCROLL_V_`, `SCROLL_H_`, `ZOOM_`) | Default V / H / Zoom | Meaning |
|---|---|---|
| `*_MIX` | `z + rx` / `x - rz` / `ry` | Axis expression: sum of `[+-][weight*]axis`, axes `x y z rx ry rz`, e.g. `0.5*rx + z`. |
| `*_SENS` | `5` / `3` / `5` | Sensitivity 0–255, `0` = channel off. (Zoom: `ZOOM_SENS`) |
| `*_DEADZONE` | `30` / `20` / `40` | Dead zone in raw sensor units. |
| `*_SIGN` | `1` | `1` or `-1` – direction. |
| `*_CURVE` | `1.0` | Exponent (`>1`: finer at light pressure, faster at strong pressure) **or** a freely defined point list `"0:0 50:0.1 200:1 350:3"` (input after dead zone : factor on the linear speed, linearly interpolated). |
| `*_SMOOTH_MS` | `0` | Smoothing (low-pass time constant in ms, independent of tick rate), `0` = off. |

Axes on the device (measured on a SpaceMouse Pro, libspnav convention): push forward/back = `z`, sideways = `x`, lift/press = `y`; tilt forward/back = `rx`, sideways = `rz`; twist the cap = `ry`. With other hardware or another `spnavrc` (e.g. `swap-yz`) simply adjust the `*_MIX` lines; `dofscroll --debug` or a measurement run shows which axis delivers what.

### Zoom detection, backend, buttons

| Key | Default | Meaning |
|---|---|---|
| `ZOOM_LATERAL` | `x z rx rz` | Axes the zoom twist must dominate (maximum, not sum). |
| `ZOOM_ENTER_MS` | `40` | How long the twist must dominate before zoom latches. |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom ends when another axis exceeds the twist by this factor. |
| `BACKEND` | `auto` | `auto` / `x11` / `uinput`. `auto` is resolved when the daemon starts (Wayland → uinput, otherwise X11/XTest; if the chosen backend fails the other is tried and logged); only a change of this key needs a daemon restart, all other keys take effect live. |
| `TICK_HZ` | `250` | Tick rate 30–1000. |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED` | `0`, `0` | Button for arrow-key mode (while held); enable on the Compact, leave off on a Pro. |
| `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | `1`, `0` | Button that is passed through as the left mouse button. |
| `MODE` | `normal` | Active mode (see "Modes"). May appear in any section. |

### Profiles per device and program

Value ranges: `*_SENS` 0–255, `*_SMOOTH_MS` 0–500, `*_CURVE` exponent 0.1–10; curve points strictly ascending (input ≤ 100000, factor ≤ 100); mix weights ≤ 100; `ZOOM_EXIT_RATIO` ≥ 1.0. All `[device:*]` sections are applied before all `[app:*]` sections (each in file order).

Ready-made example files: `examples/dofscroll-pro.conf` (SpaceMouse Pro, buttons off) and `examples/dofscroll-compact.conf` (SpaceMouse Compact, both buttons active: left = arrow-key mode, right = left click). `examples/dofscroll-multi-device.conf` is a combined file with both device profiles for operation with changing mice.

    [device:SpaceMouse Pro]     # applies when the device name contains this text
    SCROLL_V_SENS = 8

    [app:firefox]               # applies when "instance class" (WM_CLASS) contains this text
    ZOOM_SENS = 12
    SCROLL_V_CURVE = 0:0 200:1 350:3

**Switch everything off for a program:** `ENABLED = 0` (default `1`, can be overridden in every section). The daemon then produces no output at all (scroll, zoom, arrow keys, click) and releases held keys, also on program switch; every change is logged ("control disabled (profile …)" / "active again"). Useful for 3D programs such as Blender or CAD that evaluate the SpaceMouse themselves and would otherwise overlap with this bridge:

    [app:blender]
    ENABLED = 0

The pattern (`WM_CLASS` substring) is shown in the tray menu under "Program:".

**Why `[global]` stays:** device sections contain only overrides; everything else (mixes, sensitivities, curves, …) comes from `[global]`. It is also the fallback for a device without a matching section and the base for `[app:*]`. `BACKEND` is read from `[global]` at start and applies to the whole daemon; `TICK_HZ` is re-resolved on every profile switch and can therefore differ per device or program.

**Switching devices:** there is nothing to switch – the daemon picks the `[device:*]` profile from the device name reported by `spacenavd` (substring, case-insensitive). Plugging in the Compact is enough; the profile switch is logged (`journalctl --user -u dofscroll.service -f`), which also shows the actual device name. Re-learning is not needed as long as the axis mixes are right; otherwise run the wizard under "Axes" in the tray with the Compact profile.

**Example files** (`examples/`, all validated by `cmake --build build --target check-config` and CI; to use one, copy it to `~/.config/dofscroll/dofscroll.conf`, after `mkdir -p ~/.config/dofscroll`):

| File | Content |
|---|---|
| `dofscroll-pro.conf` | SpaceMouse Pro, buttons off. |
| `dofscroll-compact.conf` | SpaceMouse Compact, both buttons active. |
| `dofscroll-multi-device.conf` | Combined file with both device profiles for changing mice. |
| `default/dofscroll.conf` | Initial config seeded on first start. |
| `modes/*.conf` | Example modes seeded on first start. |

**Limits when loading:** file ≤ 1 MiB, line ≤ 8 KiB, ≤ 256 sections, ≤ 512 keys per section, ≤ 12 mix terms, ≤ 64 curve points; duplicate keys within a section are merged (the last wins); non-regular files (FIFO, directory) are rejected. A relative `XDG_CONFIG_HOME` is ignored.

**Saving (tray):** writes atomically (temp + `fsync` + `rename`), resolves a symlink and writes at the real target, keeps the file mode (new file `0600`). If the file was changed by hand in the meantime, the tray reloads it before saving and applies only its own changes to it. Profile names must not contain `]` or line breaks; a UTF-8 BOM at the start of the file is ignored.

**Format:** The new format with `[global]` and the channel keys (`SCROLL_V_SENS` …) is authoritative. The old flat file (`SCROLL_SENSITIVITY_Y`, `INVERT_Y`/`INVERT_X` (only `1` or `-1`, alias for `*_SIGN`) …) is still read; `ZOOM_INVERT = 1` corresponds to `ZOOM_SIGN = -1`. On the first save in the tray it is rewritten into the new format (comments are lost).

**Resolution order:** `[global]` → matching `[device:*]` → mode overrides → matching `[app:*]` (file order, later wins; case-insensitive substring comparison). Sections contain only the overridden keys. Find the program name with `xprop WM_CLASS` (click the window); the daemon logs every profile switch. The daemon reloads the file on SIGHUP **and** automatically when the file changes.

Program profiles need a way to read the active window: always under X11, under Wayland only with `kdotool` (KDE) – otherwise only `[global]` and `[device:*]` apply.

The easiest way to maintain everything is the tray settings window (profile selection, curve preview); editing by hand is equivalent.

### Modes

A **mode** decides what the three channels and the device buttons do. Modes are plugins: declarative files in `~/.config/dofscroll/modes/*.conf` – no recompiling needed. The file name without `.conf` is the mode id (`[a-z0-9_-]{1,32}`). **Exactly one mode is active** at any time: the one selected by the `MODE` key of the resolved profile, otherwise `normal`. `MODE` may appear in any section (`[global]`, `[device:*]`, `[app:*]`).

**Precedence:** global < device < mode overrides < app. The mode's settings overrides are applied after the device profile and before the app profile, so an `[app:*]` section still has the last word. `MODE` itself is not allowed inside a mode file.

**Built-in modes** (a file with the same name replaces the built-in one):

| Mode | Behavior |
|---|---|
| `normal` | All channels `default`, no button bindings – the classic behavior (scroll, Ctrl+scroll zoom, `NAV_BUTTON`/`CLICK_BUTTON`). |
| `off` | All channels `none` – switches control off without `ENABLED = 0`. |
| `timeline` | Vertical `none`, horizontal and twist `keys:left,right` – scrubbing and single frames for video editing. |

**File format** (same as the main config: `KEY = value`, `#`/`;` comments, no sections):

    NAME = Timeline (scrub/frames)    # display name (tray), optional
    DESCRIPTION = Video editing       # optional
    V_ACTION = none                   # vertical channel   (SCROLL_V)
    H_ACTION = keys:left,right        # horizontal channel (SCROLL_H)
    TWIST_ACTION = keys:left,right    # twist channel      (ZOOM)
    BUTTON_0 = key:space              # button 0 holds the space key
    BUTTON_1 = click
    SCROLL_H_SENS = 4                 # any settings key (same keys as in a profile)

- **Channel actions** (`V_ACTION`, `H_ACTION`, `TWIST_ACTION`): `default` | `none` | `wheel_v` | `wheel_h` | `zoom` | `keys:<neg>,<pos>`. `default` is the channel's normal behavior (V: `wheel_v`, H: `wheel_h`, twist: `zoom` with dominance detection). `zoom` = hold Ctrl + mouse wheel. `keys:<neg>,<pos>` sends key presses (`<neg>` on negative, `<pos>` on positive deflection; rate follows the deflection like in nav mode, so dead zone, curve, smoothing and sensitivity of the respective channel apply). Key names come from `src/keys.hpp` (ctrl, shift, alt, super, arrows, space, enter, esc, tab, F1–F12, a–z, 0–9, …).
- **Button actions** (`BUTTON_<n>`): `none` | `click` | `rightclick` | `middleclick` | `nav` | `key:<name>`. Mouse-button actions and `key:` hold the button/key as long as the device button is held; `nav` sends arrow keys for the V/H channels instead of scrolling while held. `<n>` must be below the **factory button count** of the device model (see "Button count").
- **Everything else** is a settings override with the same keys as in a profile (e.g. `SCROLL_H_SENS`, `ZOOM_SIGN`).
- A mode whose three channel actions are all `default` runs in the normal path (including dominance-based zoom and `NAV_BUTTON`/`CLICK_BUTTON` from the settings); its button bindings apply in addition.
- On a mode change, suspend, `ENABLED = 0` and daemon exit, keys and buttons held by a mode are released.

**Limits and robustness:** at most 64 mode files, each ≤ 64 KiB, ≤ 128 overrides per file; only regular files; invalid lines or files are skipped with a warning in the log. The daemon reloads the mode directory automatically when its contents change.

**Timeline mode (video editing, e.g. DaVinci Resolve):** `MODE = timeline` (default `normal`). Sideways push/tilt (channel `SCROLL_H_*`) = **scrubbing**, twisting the cap (channel `ZOOM_*`) = **single frames**; both send left/right arrow, with the frames per second following the deflection. No scrolling, no zoom, vertical push is ignored. The twist direction can be reversed with `ZOOM_SIGN = -1`. As a program profile (example in `examples/dofscroll-pro.conf`):

    [app:resolve]
    MODE = timeline
    SCROLL_H_SENS = 4     # scrubbing
    ZOOM_SENS = 1         # single frames (finer than the default)

Prerequisite in Resolve: the timeline (or the viewer) has keyboard focus, where left/right arrow moves by one frame.

---

## 5. Build

Required packages (RHEL 9 + EPEL):

    sudo dnf install -y epel-release   # if EPEL is not active yet
    sudo dnf install -y gcc-c++ make cmake spacenavd libspnav-devel \
        libX11-devel libXtst-devel \
        gtk3-devel libappindicator-gtk3-devel gettext

(`gettext` is only needed for the translations. If your distribution has no suitable `cmake` (3.16+), `pip install --user cmake` works.)

The build is CMake only; `dofscroll` and `dofscroll-tray` are built hardened and land in `build/`:

    cmake -S . -B build
    cmake --build build --target check-deps   # checks dependencies, prints the dnf/apt line if needed
    cmake --build build -j4                   # both binaries
    ctest --test-dir build --output-on-failure   # unit and scenario tests
    cmake --build build --target check        # full quality gate (section 8)

Options (`-D...` at configure time): `DOFSCROLL_TRAY=OFF` (daemon only, no GTK), `DOFSCROLL_HARDEN`, `DOFSCROLL_SANITIZE`, `DOFSCROLL_WERROR`, `DOFSCROLL_REQUIRE_ASAN`. Build only the daemon: `cmake --build build --target dofscroll`. Validate a single config: `build/tests/check_config FILE`. Clean: `rm -rf build`.

Further targets: `cmake --install build --prefix /usr/local` / `--target uninstall` (DESTDIR honored), `install-udev` (uinput rule, see `packaging/README-uinput.md`), `install-user-links` (only prints the symlink commands from section 6), `lint` (cppcheck), `check-fast`. Linux packages (RPM/DEB/TGZ): `cpack --config build/CPackConfig.cmake`. CI: `.github/workflows/ci.yml`.

---

## 6. Installation / setup

Basic principle: **nothing is copied to `/usr/local` or `~/.config/systemd/user`** – there are only symlinks back into the cloned repo. The repo working directory is thereby the single source of truth; a rebuild or a `git pull` takes effect immediately, without a stale copy remaining anywhere.

`/usr/local/bin` and `/usr/local/share` belong to root – hence `sudo`:

    sudo ln -s "$(pwd)/build/dofscroll" /usr/local/bin/dofscroll
    sudo ln -s "$(pwd)/build/dofscroll-tray" /usr/local/bin/dofscroll-tray
    sudo mkdir -p /usr/local/share/dofscroll-tray/icons
    sudo ln -s "$(pwd)/icons/dofscroll-tray.svg" /usr/local/share/dofscroll-tray/icons/dofscroll-tray.svg

Link (instead of copy) the systemd units (they live as templates in `systemd/` in this repo):

    mkdir -p ~/.config/systemd/user
    ln -s "$(pwd)/systemd/dofscroll.service" ~/.config/systemd/user/dofscroll.service
    ln -s "$(pwd)/systemd/dofscroll-tray.service" ~/.config/systemd/user/dofscroll-tray.service

Enable `spacenavd` (system-wide driver service, comes with the package) once:

    sudo systemctl enable --now spacenavd

Enable the two user services – they then start automatically with every graphical session:

    systemctl --user daemon-reload
    systemctl --user enable --now dofscroll.service
    systemctl --user enable --now dofscroll-tray.service

The config file `~/.config/dofscroll/dofscroll.conf` is created on the first start of the tray from the shipped examples (see "First-start seeding"); the directory is created by the tray unit via `ConfigurationDirectory=` (manually: `mkdir -p -m 0700 ~/.config/dofscroll`). Adjust values as needed (see section 4) – either by hand or simply by clicking through the sliders in the tray settings window. The file deliberately stays a real copy, not a symlink – it is purely user-specific and not part of the repo.

**Updating after a code change:** thanks to the symlinks a rebuild in the repo directory is enough – no new `cp`/`ln` needed. The running process keeps the binary open (`Text file busy`), so stop before rebuilding:

    systemctl --user stop dofscroll.service dofscroll-tray.service
    cmake --build build -j4
    systemctl --user start dofscroll.service dofscroll-tray.service

---

## 7. Moving to a new system

Checklist for a fresh RHEL 9 system (or a reinstall):

1. **Clone the repo**

       git clone <repository-url> ~/dofscroll
       cd ~/dofscroll

2. **Install dependencies** and **build both binaries** (see section 5).

3. **Install** (set symlinks for binaries/icon/systemd units, activate `spacenavd`; see section 6). Since everything is only linked instead of copied, the clone path from step 1 must stay stable afterwards – if the repo directory is moved or renamed later, all symlinks under `/usr/local` and `~/.config/systemd/user` point nowhere and must be set again.

4. **Take over the config.** `~/.config/dofscroll/dofscroll.conf` is deliberately not part of the repo (purely user-specific). Options:
   - Copy the old file from the previous system to `~/.config/dofscroll/dofscroll.conf` (e.g. via `scp`; first `mkdir -p -m 0700 ~/.config/dofscroll`; an old file `~/.config/spnav-scroll/spnav-scroll.conf` or `~/.config/spnav-scroll.conf` is taken over by the tray automatically at start), also copy `~/.config/dofscroll/modes/`, then `systemctl --user kill -s HUP dofscroll.service` – **or**
   - Start without a copy: the tray seeds the shipped example config and modes, fine tuning afterwards through the tray settings window.

5. **Enable the user services** (see section 6, last block) and log out/in once, or reach `graphical-session.target` again, so that `WantedBy=graphical-session.target` takes effect.

6. **Verify:**
   - Connect the SpaceMouse, `systemctl --user status dofscroll.service` → `active (running)`.
   - Tray icon visible (under GNOME install/enable the AppIndicator extension if needed), icon identical to the one in the settings window/task bar.
   - Test scrolling/zooming/nav mode/click button in any window.
   - In case of problems: `journalctl --user -u dofscroll.service -f` and `journalctl --user -u dofscroll-tray.service -f`.

7. **Avoid the autostart trap:** `dofscroll-tray.service` has `After=... dofscroll.service` and `PartOf=graphical-session.target`, so it needs a running graphical session – a plain SSH login without a graphical session does not start it (this is intended; do not change it without understanding the `After`/`PartOf` chain).

---

## 8. Quality and security ("quality gate")

- `cmake --build build --target check` is the central gate (local, no root needed): `-Werror` build, unit/scenario tests, sanitizers (ASAN/UBSAN, skipped locally if libasan is missing), `cppcheck`, validation of the example configs, hardened build and hardening check (`scripts/check-hardening.sh`: PIE, full RELRO, NX, stack protector, FORTIFY). `--target check-fast` is the quick variant (-Werror build + tests).
- All binaries are built hardened (`DOFSCROLL_HARDEN` in `CMakeLists.txt`). After changing the flags re-run the build (or `rm -rf build`).
- CI: a GitHub Actions workflow is planned (`build`, `test`, `check` with `REQUIRE_ASAN=1`, config self-tests, secret scan; as a required check for `main`, see `docs/security.md`).
- Pre-commit hook: `scripts/install-hooks.sh` (runs `scripts/secret-scan.sh` and the CMake target `check-fast`).
- `docs/security.md`: threat model, findings with severity, systemd hardening (the units in `systemd/` are hardened; effective only after `systemctl --user daemon-reload` + restart, rollback via drop-in see there) and `uinput` permissions.

## 9. Settings in the vendor driver

dofscroll translates the *already processed* values that the vendor driver hands out. If the vendor driver also turns the same input into scrolling or panning for the same program, everything happens **twice** (or fights). So the driver-side settings matter.

### Linux: `spacenavd` / `spnavrc`

- `spacenavd` is configured through `/etc/spnavrc` (or `spnavd.conf` depending on the distribution package; see the `spacenavd` documentation of the version you run). It applies its own sensitivity, dead zone, axis inversion and axis swapping (e.g. `swap-yz`) **before** dofscroll sees any value. dofscroll then applies its own channel settings on top.
- Recommendation: keep the `spacenavd` settings at their defaults and tune in dofscroll (profiles, curves, per-program modes). If you change `spacenavd` settings (inversion, axis swap), adjust the `*_MIX` lines and re-run the "Learn axes" wizard, because the axis numbers change meaning.
- `spacenavd` itself does not scroll anything in ordinary applications. But other programs that talk to it directly (Blender, CAD, ...) read the same device concurrently; for those switch dofscroll off with an `[app:*]` section containing `ENABLED = 0` (or `MODE = off`), so input is not applied twice.
- Restart `spacenavd` (`sudo systemctl restart spacenavd`) after editing its configuration; the dofscroll daemon reconnects automatically.

### Windows and macOS: 3Dconnexion driver (planned)

Windows and macOS support is **planned and not implemented yet**; the following is guidance for when the port exists. On those systems the official 3Dconnexion driver is installed and delivers the device data. To keep input from being applied twice:

- In the driver's settings/application profiles, turn off the driver's own scroll/pan/zoom mapping for the programs in which dofscroll should do the job (or turn off the generic navigation for those programs), and keep the profile for 3D applications (CAD, Blender, ...) enabled there while switching dofscroll off for them with `ENABLED = 0`.
- Keep the driver's axis, sensitivity and inversion settings neutral where possible and tune in dofscroll instead; if changed, re-run the "Learn axes" wizard.
- Details and open questions of the driver research (application profiles, axes, sensitivity, what is documented and what is unclear): `docs/windows-mac.md`.

## 10. Future improvements

1. Extended button mapping for the additional SpaceMouse buttons (e.g. shortcuts) via mode files – the Compact already uses its two buttons for nav mode and click; the Pro has 15 buttons.
2. Active-window detection for native Wayland windows without `kdotool` (e.g. via KWin scripting).
3. Windows and macOS ports (see section 9 and `docs/windows-mac.md`).

## License and trademarks

dofscroll is licensed under the **GNU General Public License, version 3 or (at your option) any later version** (SPDX: `GPL-3.0-or-later`), see `LICENSE`. Licenses of the libraries it builds on are listed in `THIRD-PARTY-NOTICES.md`.

"3Dconnexion" and "SpaceMouse" are trademarks of 3Dconnexion GmbH or their respective owners. dofscroll is an independent project and is neither endorsed by nor affiliated with 3Dconnexion; the names are used descriptively only, to name the supported hardware.
