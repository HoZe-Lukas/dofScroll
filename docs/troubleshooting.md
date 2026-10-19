<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Troubleshooting

## Quick checks

    systemctl status spacenavd
    systemctl --user status dofscroll.service dofscroll-tray.service
    journalctl --user -u dofscroll.service -n 50

## Logs and status files

| What | Where |
|---|---|
| Daemon log | `journalctl --user -u dofscroll.service` (add `-f` to follow, `-n 50` for the last lines, `-b` for this boot) |
| Tray log | `journalctl --user -u dofscroll-tray.service` |
| Log copy for the tray | `$XDG_RUNTIME_DIR/dofscroll.log` (usually `/run/user/<uid>/`, at most 300 lines) |
| Status file | `$XDG_RUNTIME_DIR/dofscroll-status`, lines `key=value`: `profile`, `device`, `app`, `enabled`, `paused`, `devices_seen`, `updated`. Removed on clean exit. |
| Config | `~/.config/dofscroll/dofscroll.conf` and `~/.config/dofscroll/modes/` |

### Reading the journal

Lines carry the daemon's messages. Look for:

- the device name and button count after the SpaceMouse connects (you need the name for
  `[device:*]` sections);
- profile switches, with the matched device and app;
- "control disabled" and "active again" (`ENABLED = 0` or `MODE = off` in effect);
- warnings about skipped config lines or mode files;
- connection retries to `spacenavd`.

For button indices and the live axis values, stop the service and run the daemon in a terminal:

    systemctl --user stop dofscroll.service
    dofscroll --debug

It prints the axes about ten times per second and every button press with its index. Start the
service again afterwards.

## Problems

### Nothing happens

1. Is `spacenavd` running? `systemctl status spacenavd`. If not:
   `sudo systemctl enable --now spacenavd`. The daemon starts without it and reconnects with a
   backoff of up to 5 s, so you need no restart.
2. Does `spacenavd` see the device? Check `journalctl -u spacenavd` and that the device is
   plugged in. `lsusb` should list a 3Dconnexion device. With two SpaceMice, `spacenavd` reports
   only one.
3. Is the user unit running? `systemctl --user status dofscroll.service`. If it is
   "not found" after a package install, the unit was not installed where systemd looks. Follow
   step 3 of the setup in [installation.md](installation.md#2-first-time-setup).
4. Does the unit fail with status 203/EXEC? The unit's `ExecStart` path is wrong
   (`/usr/local/bin` versus `/usr/bin`). Fix it as in the same step.
5. Is output paused or disabled? The status file shows `enabled=0` or `paused=1`. The daemon
   pauses while the tray's "Axes" tab or the wizard is open. An `[app:*]` section with
   `ENABLED = 0`, or `MODE = off`, switches it off for that program.
6. Does another program grab the device (Blender, CAD)? Switch dofscroll off for it with
   `[app:<name>]` `ENABLED = 0`.

### The service does not start with the session

The units are `WantedBy=graphical-session.target`. A plain SSH login has no graphical session
and does not start them. Log in graphically or start them by hand. After
`systemctl --user enable`, log out and in once.

### uinput: permission denied (Wayland)

`BACKEND = auto` picks uinput under Wayland. The user needs write access to `/dev/uinput`.

1. Install the udev rule, see [installation.md](installation.md#2-first-time-setup) and
   [packaging/README-uinput.md](../packaging/README-uinput.md). The rule uses `uaccess`, so the
   user of the active local session gets an ACL.
2. `sudo udevadm control --reload && sudo udevadm trigger`, then log out and in.
3. If `/dev/uinput` does not exist: `sudo modprobe uinput`.
4. Check: `ls -l /dev/uinput` shows a `+` (ACL), `getfacl /dev/uinput` lists your user with `rw-`.

Do not add yourself to the `input` group. It gives access to all keyboards (reason in
[packaging/README-uinput.md](../packaging/README-uinput.md)). An SSH session gets no `uaccess` ACL.

### Wayland versus X11

| | X11 | Wayland |
|---|---|---|
| Backend | XTest | uinput (needs the udev rule) |
| `[app:*]` profiles | Yes (`WM_CLASS`) | Only with `kdotool` on KDE. Native Wayland windows may not be found. |

Without an active-window query only `[global]` and `[device:*]` apply. To force a backend, set
`BACKEND = x11` or `uinput` in `[global]` and restart the daemon. The log says which backend
was chosen and why.

### Tray icon is missing

The tray uses AppIndicator (StatusNotifierItem).

- KDE Plasma: supported.
- GNOME: install and enable the AppIndicator extension (on RHEL:
  `gnome-shell-extension-appindicator`; the package name on other distributions is unverified).
- Check `journalctl --user -u dofscroll-tray.service` for errors.
- Builds without GTK (`-DDOFSCROLL_TRAY=OFF`) have no tray. Edit the config by hand.

The daemon works without the tray. The tray is only needed for the settings window and the first
start seeding.

### Settings window or menus are in German (or another language)

The UI follows the locale (`LC_MESSAGES`, `LANGUAGE`). To force English:

    systemctl --user edit dofscroll-tray.service

and add

    [Service]
    Environment=LANGUAGE=C

or start the tray by hand with `LANGUAGE=C dofscroll-tray`. Daemon log lines are always English.
Translations are looked up in `$DOFSCROLL_LOCALEDIR`, `../share/locale` next to the binary,
`/usr/local/share/locale` and `/usr/share/locale`. A missing catalog shows English.

### Scrolling is inverted or on the wrong axis

Set `<CH>_SIGN = -1` or change the `<CH>_MIX` line, or run "Learn axes" in the tray. If you changed
`spacenavd` settings (`/etc/spnavrc`), the axis meaning changed too.

### Keys stay pressed after a crash

The daemon releases Ctrl, arrows and the left mouse button at start, and the unit runs
`dofscroll --release-keys` after it stops. If a key still hangs, restart the service.

### Config changes do not apply

The daemon reloads when the file changes. Force it: `systemctl --user kill -s HUP dofscroll.service`.
Check the journal for a rejected line. `BACKEND` needs a restart. Validate a file with
`build/tests/check_config FILE` (source build only).

### Duplicate input in a 3D program

Both dofscroll and the program read the device. Add `[app:<WM_CLASS>]` with `ENABLED = 0`.

## Reporting a problem

Include the dofscroll version (`dofscroll --version`), distribution, X11 or Wayland, the device
name from the journal, and the relevant journal lines. Remove personal data from the logs first.
