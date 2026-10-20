<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Quick start

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

The first five minutes on Linux (X11, the tested setup). This assumes you finished the [Installation](Installation) steps and met the [Requirements](Requirements) (on Windows and macOS see the per-OS checklist there).

1. **Driver.** `systemctl status spacenavd` must be active. If not: `sudo systemctl enable --now spacenavd`. Plug in the SpaceMouse; `lsusb` should list a 3Dconnexion device.
2. **Units.** `systemctl --user enable --now dofscroll.service dofscroll-ui.service`.
3. **Check the log.** `journalctl --user -u dofscroll.service -f` shows the device name and button count when the device connects. You need the device name for `[device:*]` sections.
4. **Try it.** Push the cap: the window under the pointer scrolls. Twist: zoom (Ctrl + scroll). Nothing happens? See [Troubleshooting](Troubleshooting).
5. **Tray.** Right-click the tray icon and open "Settings..." (the interface starts minimized, no window opens by itself): sensitivity, dead zone and direction per channel. Changes apply immediately. GNOME needs an AppIndicator extension to show the icon.

## What the seeded config gives you

On first start the tray writes `~/.config/dofscroll/dofscroll.conf` from [examples/default/dofscroll.conf](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/dofscroll.conf): a `[global]` section with the three channels (vertical scroll, horizontal scroll, zoom) and a `[device:...]` section each for SpaceMouse Pro and SpaceMouse Compact. The joystick modes are built into the program (nothing is copied):

| Joystick mode | What it does |
|---|---|
| `browse` | Page up/down, left/right, twist zooms |
| `slides` | Up/down pages through slides (Page Up / Page Down), gentle rate |
| `media` | Up/down = volume keys, sideways = seek, twist = `j`/`l` |
| `map` | Arrow keys to pan, twist presses `minus`/`plus` |
| `precision` | Normal behavior with low sensitivities |
| `tabs` | Sideways switches tabs (Ctrl+Page Up / Page Down) |
| `timeline` | Sideways and twist press the left/right arrow keys; button 1 is Space, button 2 is Home (only in this joystick mode) |

Also built in: `normal` and `off`. Switch the joystick mode from the tray "Joystick mode" menu, or set `MODE = browse` in a profile. Read a joystick mode file before you use it, see [Joystick modes](Modes).

## Next steps

- Different behavior per program or device: [Profiles](Profiles)
- Buttons: map always-the-same keys and clicks in the vendor driver, use dofscroll for mode-dependent bindings, joystick mode switching and nav: [Buttons](Buttons)
- Every key: [Configuration reference](Configuration-reference)

Real-device behavior can only be confirmed by you on your hardware.
