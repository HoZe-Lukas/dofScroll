<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Troubleshooting

A button does nothing or input arrives twice: check the driver mapping first, see [Buttons](Buttons). dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles.

Full guide: [docs/troubleshooting.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/troubleshooting.md). This page covers Linux; the Windows and macOS ports are unverified on hardware (see their checklists under [Installation](Installation)).

## Quick checks

```sh
systemctl status spacenavd
systemctl --user status dofscroll.service dofscroll-ui.service
journalctl --user -u dofscroll.service -n 50
```

## Logs and status files

| What | Where |
|---|---|
| Daemon log | `journalctl --user -u dofscroll.service` (`-f` to follow) |
| Interface log | `journalctl --user -u dofscroll-ui.service` |
| Log copy for the tray | `$XDG_RUNTIME_DIR/dofscroll.log` (at most 300 lines) |
| Status file | `$XDG_RUNTIME_DIR/dofscroll-status` (`profile`, `device`, `app`, `enabled`, `paused`, `devices_seen`, `updated`); removed on clean exit |
| Config | `~/.config/dofscroll/dofscroll.conf` and `~/.config/dofscroll/modes/` |

For button indices and live axis values, stop the service and run `dofscroll --debug` in a terminal; it prints the axes about ten times per second and every button press with its index.

## Nothing happens

1. `spacenavd` running? `sudo systemctl enable --now spacenavd`. The daemon reconnects by itself (backoff up to 5 s).
2. Does `spacenavd` see the device? `journalctl -u spacenavd`; `lsusb` should list a 3Dconnexion device.
3. User unit "not found": `systemctl --user daemon-reload`, then check the setup steps in [Installation](Installation).
4. Status 203/EXEC: the program in `ExecStart` does not exist. Check `systemctl --user cat dofscroll.service`.
5. Output paused or disabled? The status file shows `enabled=0` or `paused=1` (paused while the tray's "Axes" tab or the wizard is open). An `[app:*]` section with `ENABLED = 0` or `MODE = off` also switches it off.
6. Another program grabs the device (Blender, CAD)? Use `[app:<name>]` with `ENABLED = 0`.
7. Does the device send reports at all? `dofscroll-hidprobe --dump`, see [Devices](Devices).

## Service does not start with the session

The units are `WantedBy=graphical-session.target`; a plain SSH login has no graphical session. Log in graphically or start them by hand. After `systemctl --user enable`, log out and in once.

## uinput: permission denied (Wayland)

`BACKEND = auto` picks uinput under Wayland. The user needs write access to `/dev/uinput`. The packages do not activate the udev rule: copy it yourself (see [Installation](Installation)), reload udev, log out and in. If `/dev/uinput` is missing: `sudo modprobe uinput`. Check with `ls -l /dev/uinput` (a `+` means an ACL) and `getfacl /dev/uinput`. Do not add yourself to the `input` group. An SSH session gets no `uaccess` ACL.

## Wayland versus X11

| | X11 | Wayland |
|---|---|---|
| Backend | XTest | uinput (opt-in udev rule) |
| `[app:*]` profiles | Yes | Only with `kdotool` on KDE; native Wayland windows may not be found |

Force a backend with `BACKEND = x11` or `uinput` in `[global]` and restart the daemon. The log says which backend was chosen and why.

## Other problems

- **Tray icon missing:** KDE Plasma is supported; GNOME needs an AppIndicator extension. A build from source without Qt6 has no tray (the release packages bundle Qt). Without a tray the Qt interface opens its window minimized; it also starts minimized to the tray, so no window appears at start.
- **UI in German:** the UI follows the locale; force English with `LANGUAGE=C`.
- **Wrong direction or axis:** set `<CH>_SIGN = -1`, change `<CH>_MIX`, or run "Learn axes" in the tray. Changes in `/etc/spnavrc` change axis meaning too.
- **Keys stay pressed after a crash:** run `dofscroll --release-keys` or restart the service. After SIGKILL under X11 keys held through XTest are not released (known limitation, see [Security](Security)).
- **Scrolling happens in another user's session:** the 3D mouse is shared, so every logged-in user's daemon sees it; each pauses while its own session is not the active one (`session=inactive` in the status file, `session inactive: output paused` in the log). If it still happens, check that `XDG_SESSION_ID` is set for the service, see [Security](Security).
- **Config changes do not apply:** `systemctl --user kill -s HUP dofscroll.service` and check the journal for a rejected line; `BACKEND` needs a restart.
- **Doubled input in a 3D program:** `[app:<WM_CLASS>]` with `ENABLED = 0`.

## Reporting a problem

Include `dofscroll --version`, distribution, X11 or Wayland, the device name from the journal and the relevant journal lines. Remove personal data from logs first. Open an issue at https://github.com/HoZe-Lukas/dofScroll/issues (security problems: see [Security](Security)).
