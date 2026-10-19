<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Quick start

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

The first five minutes on Linux (X11, the tested setup). This assumes you finished the [Installation](Installation) steps.

1. **Driver.** `systemctl status spacenavd` must be active. If not: `sudo systemctl enable --now spacenavd`. Plug in the SpaceMouse; `lsusb` should list a 3Dconnexion device.
2. **Units.** `systemctl --user enable --now dofscroll.service dofscroll-tray.service`.
3. **Check the log.** `journalctl --user -u dofscroll.service -f` shows the device name and button count when the device connects. You need the device name for `[device:*]` sections.
4. **Try it.** Push the cap: the window under the pointer scrolls. Twist: zoom (Ctrl + scroll). Nothing happens? See [Troubleshooting](Troubleshooting).
5. **Tray.** Right-click the tray icon and open "Settings...": sensitivity, dead zone and direction per channel. Changes apply immediately. GNOME needs an AppIndicator extension to show the icon.

## What the seeded config gives you

On first start the tray writes `~/.config/dofscroll/dofscroll.conf` from [examples/default/dofscroll.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/dofscroll.conf): a `[global]` section with the three channels (vertical scroll, horizontal scroll, zoom) and a `[device:...]` section each for SpaceMouse Pro and SpaceMouse Compact. It also copies three example modes into `~/.config/dofscroll/modes/`:

| Mode | What it does |
|---|---|
| `browse-keys` | Page up/down, left/right, twist zooms |
| `cycle-modes` | Normal behavior; button 2 switches to the next mode |
| `timeline` | Sideways and twist press the left/right arrow keys; button 1 is Space, button 2 is Home (only in this mode) |

The built-in modes are `normal`, `off` and `timeline`. Switch the mode from the tray "Mode" menu, or set `MODE = browse-keys` in a profile. Read a mode file before you use it, see [Modes](Modes).

## Next steps

- Different behavior per program or device: [Profiles](Profiles)
- Buttons: map always-the-same keys and clicks in the vendor driver, use dofscroll for mode-dependent bindings, mode switching and nav: [Buttons](Buttons)
- Every key: [Configuration reference](Configuration-reference)

Real-device behavior can only be confirmed by you on your hardware.
