<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Profiles for programs and devices

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Profiles are sections in `dofscroll.conf` that override only the keys they list. Full text: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

| Section | Applies when |
|---|---|
| `[global]` | Always |
| `[device:<text>]` | The device name from `spacenavd` contains `<text>` (case-insensitive substring) |
| `[app:<text>]` | The active window's class contains `<text>` (case-insensitive substring) |

## Precedence

```
[global]  <  matching [device:*]  <  active mode's overrides  <  matching [app:*]
```

All device sections are applied before all app sections, each in file order, so a later matching section wins. The mode is chosen by `MODE` from the resolved profile (global, device or app). An app section can select a mode and still has the last word over that mode's overrides.

## Example

```
[global]
SCROLL_V_SENS = 5

[device:SpaceMouse Pro]
SCROLL_V_SENS = 8

[app:firefox]
ZOOM_SENS = 12
SCROLL_V_CURVE = 0:0 200:1 350:3

[app:blender]
ENABLED = 0                # 3D programs read the device themselves

[app:resolve]
MODE = timeline
```

## Editing a profile in the interface

The editor of the selected profile on the Profiles page has the tabs General (joystick mode, output switch), Channels (scroll, zoom, curve, zoom detection; per row the value, where it comes from and Reset), Buttons and Overrides. Global has the same tabs: everything can be set in Global like in any other profile. The header says which profile is edited and that everything not changed in a device or program profile is inherited from Global; a value of Global that profiles override says "N profiles override this". There is no Save button: every change is saved by itself and an invalid value is not written. Two search boxes filter the profile list (by name) and the settings of the selected profile (by label or key). "Export..." writes the selected profile as a `.conf` file.

### Inheritance

`[global]` is the base; a `[device:*]` or `[app:*]` section holds only what it changes. Precedence: Global < device < joystick mode overrides < program. `MODE`, every channel key and every `BUTTON_<n>` can be set in `[global]` too. Buttons are inherited per button (`BUTTON_<n> = none` in a profile switches an inherited button off there). See [Configuration reference](Configuration-reference). Details: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

## Finding the program name

On X11, run `xprop WM_CLASS` and click the window. The daemon logs every profile switch (`journalctl --user -u dofscroll.service`). On Windows the key is the lower-cased executable name (for example `firefox.exe`); on macOS the bundle identifier or app name. Both are unverified on hardware.

## Limits

- Program profiles need an active-window query: always on X11, on Wayland only with `kdotool` (KDE). Otherwise only `[global]` and `[device:*]` apply.
- With two SpaceMice connected, `spacenavd` reports only one device, so `[device:*]` applies to both. Direct HID access (Windows, macOS ports) lifts this, but is unverified on hardware.
- 3D programs that read the device directly get doubled input; switch dofscroll off for them with `ENABLED = 0` or `MODE = off`.

See also [Configuration reference](Configuration-reference), [Modes](Modes).
