<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Buttons

## Buttons and the vendor driver

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles.

Examples: a button that always sends Esc belongs in the driver. A button that sends Space (play/pause) only while the `timeline` mode is active, a button with `mode:next` and a button with `nav` belong in dofscroll.

- **Linux, spacenavd:** in `/etc/spnavrc`, `kbmapN = <keysym>` maps button N (zero-based) to a key, for example `kbmap0 = Escape` (experimental upstream). Restart spacenavd or send it SIGHUP. Source: spacenavd's upstream `doc/example-spnavrc`; a mouse-click mapping was not found there (not verified).
- **Windows and macOS:** assign buttons in the 3Dconnexion driver's settings. Menu names differ per version and were not verified here.

The driver works in every program and keeps working when dofscroll is stopped. dofscroll's own `key:`, `click`, `rightclick` and `middleclick` actions stay fully supported: use them for mode-specific bindings (active only while that mode is active), or if you have no driver.

If the driver also maps the axes to scroll or pan in some programs, turn that off per application to avoid double input. Details: [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#buttons-and-the-vendor-driver) and [docs/windows-mac.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/windows-mac.md).

## Binding buttons in dofscroll

Buttons are numbered **from 1**: `BUTTON_1` is the first button. `BUTTON_0` is rejected ("buttons are numbered from 1"). At most 32 buttons.

```
BUTTON_1 = nav
BUTTON_3 = mode:next
# in a mode file such as timeline.conf:
BUTTON_2 = key:space
```

Actions: `none`, `click`, `rightclick`, `middleclick`, `nav`, `key:<name>`, `mode:<id>`, `mode:next`. Details are on the [Modes](Modes) page. `BUTTON_<n>` works in the main config (in any section) and in mode files; a binding in the active mode wins over the main config for the same button.

## Factory button counts

`n` must not exceed the **factory button count** of the device model, taken from `factoryButtonCount()` in [src/device_db.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/device_db.cpp), not from the count `spacenavd` reports. Unknown models use the reported count.

| Model | Buttons | Basis |
|---|---|---|
| SpaceMouse Compact | 2 | 3Dconnexion product page |
| SpaceMouse Wireless | 2 | 3Dconnexion product page |
| SpaceMouse Pro | 15 | 3Dconnexion product page |
| SpaceMouse Pro Wireless | 15 | 3Dconnexion buy page |
| SpaceMouse Enterprise | 31 | Manufacturer feature text repeated by retailers; one retailer lists 15 |
| SpaceNavigator | 2 | Manufacturer product copy as published by distributors |

These counts come from vendor information. Only the Pro and the Compact have been tested on hardware. SpaceExplorer and SpacePilot Pro are not listed because no count could be verified. Source: [docs/devices.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/devices.md).

## Older keys

`NAV_BUTTON` and `CLICK_BUTTON` (with `..._ENABLED`) take the raw button index as `spacenavd` reports it; the daemon in `--debug` mode prints the index of every pressed button. New setups should use `BUTTON_<n>` with `nav` and `mode:`.

See [Devices](Devices) and [Configuration reference](Configuration-reference).
