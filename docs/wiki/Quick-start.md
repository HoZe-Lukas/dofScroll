<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Quick start

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

The first five minutes (Linux X11 is the tested setup). This assumes you installed the package or installer and met the [Requirements](Requirements). **The first start is just opening dofScroll** from the start menu, the application launcher or the Applications folder: no terminal.

1. **Open dofScroll.** The interface starts the daemon by itself (about 6 seconds after it starts, if none runs) and the first start shows a short **setup guide** with live checks: "dofscroll is running", "SpaceMouse found", "Start at login" (a check box, on by default) and, on macOS, the two permissions (Input Monitoring, Accessibility) with a button that opens System Settings. Skip it or finish it any time; **Show setup guide** on the Overview opens it again. The Linux packages never enable the `spacenavd` driver service for you; if the guide says it is off, it shows the one command to copy (`sudo systemctl enable --now spacenavd`). **Nothing is changed until you press Finish** (this includes Start at login).
2. **Plug in the SpaceMouse.** The guide's step "SpaceMouse found" turns to Done. Without a desktop, or for a TGZ install, the old commands still work: `sudo systemctl enable --now spacenavd`, then `systemctl --user enable --now dofscroll.service dofscroll-ui.service`.
3. **Check the log (optional).** `journalctl --user -u dofscroll.service -f` shows the device name and button count when the device connects. You need the device name for `[device:*]` sections.
4. **Try it.** Push the cap: the window under the pointer scrolls. Twist: zoom (Ctrl + scroll; the key is `ZOOM_MODIFIER`). Scrolls the wrong way? See the [FAQ](FAQ). Nothing happens? See [Troubleshooting](Troubleshooting).
5. **Tray.** Right-click the tray icon and open "Settings..." (after the first start the interface starts minimized, no window opens by itself): sensitivity, dead zone and direction per channel. Changes apply immediately. GNOME needs an AppIndicator extension to show the icon.

## What the seeded config gives you

On first start the tray writes `~/.config/dofscroll/dofscroll.conf` (global settings; the profiles are separate files in `devices/` and `apps/`) from `examples/default/<os>/dofscroll.conf` (own file for [Linux](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/linux/dofscroll.conf), [Windows](https://github.com/HoZe-Lukas/dofScroll/blob/main/examples/default/windows/dofscroll.conf) and macOS, experimental): a `[global]` section with the three channels (vertical scroll, horizontal scroll, zoom). The axis order differs per system: the Windows default scrolls by pushing and tilting; the macOS default (experimental, never run on a Mac) uses the Windows values. When the interface sees your SpaceMouse for the first time, it writes the profile of that device as `devices/<name>.conf` (button 1 is navigation). The joystick modes are built into the program (nothing is copied):

| Joystick mode | What it does |
|---|---|
| `browse` | Page up/down, left/right, twist zooms |
| `slides` | Up/down pages through slides (Page Up / Page Down), gentle rate |
| `media` | Up/down = volume keys, sideways = seek, twist = `j`/`l` |
| `map` | Arrow keys to pan, twist presses `minus`/`plus` |
| `precision` | Normal behavior with low sensitivities |
| `tabs` | Sideways switches tabs (Ctrl+Page Up / Page Down) |
| `volume` | Up/down = Volume up / Volume down (not sent on macOS) |
| `tracks` | Sideways = previous / next track, one per push (not sent on macOS) |
| `sheet` | Arrow keys for spreadsheet cells, twist pages up/down |
| `photo` | Sideways = previous/next image, up/down zooms |
| `zoom-pan` | Up/down zooms, twist scrolls sideways |
| `vertical-only` | Vertical scroll only, no sideways scroll or zoom |
| `fast` | Normal behavior with high sensitivities |
| `timeline` | Sideways push, sideways tilt and twist press the left/right arrow keys; up/down is off. No button bindings. |

The full table, by the six motions of the cap, is on [Built-in joystick modes](Built-in-modes). Also built in: `normal` and `off`. Switch the joystick mode from the tray "Joystick mode" menu, or set `MODE = browse` in a profile. Read a joystick mode file before you use it, see [Joystick modes](Modes).

## Next steps

- Different behavior per program or device: [Profiles](Profiles)
- Buttons: map always-the-same keys and clicks in the vendor driver, use dofscroll for joystick-mode-dependent bindings, joystick mode switching and nav: [Buttons](Buttons)
- Every key: [Configuration reference](Configuration-reference)

Real-device behavior can only be confirmed by you on your hardware.
