<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Buttons

Buttons are numbered **from 1**: `BUTTON_1` is the first button. `BUTTON_0` is rejected ("buttons are numbered from 1"). At most 32 buttons.

```
BUTTON_1 = click
BUTTON_2 = key:esc
BUTTON_3 = mode:next
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

`NAV_BUTTON` and `CLICK_BUTTON` (with `..._ENABLED`) take the raw button index as `spacenavd` reports it; the daemon in `--debug` mode prints the index of every pressed button. New setups should use `BUTTON_<n>` with `nav` and `click`.

See [Devices](Devices) and [Configuration reference](Configuration-reference).
