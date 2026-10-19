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
   "not found", run `systemctl --user daemon-reload` and check step 3 of the setup in
   [installation.md](installation.md#2-first-time-setup).
4. Does the unit fail with status 203/EXEC? The program in the unit's `ExecStart` does not
   exist. A package install points at `/usr/bin`, the symlink flow from the build tree at
   `/usr/local/bin` (see "Run from the build tree"). Check with `systemctl --user cat dofscroll.service`.
5. Is output paused or disabled? The status file shows `enabled=0` or `paused=1`. The daemon
   pauses while the tray's "Axes" tab or the wizard is open. An `[app:*]` section with
   `ENABLED = 0`, or `MODE = off`, switches it off for that program.
6. Does another program grab the device (Blender, CAD)? Switch dofscroll off for it with
   `[app:<name>]` `ENABLED = 0`.
7. Does the device send reports at all? `dofscroll-hidprobe --dump` shows the raw reports (see
   [devices.md](devices.md#verifying-a-device-with-hidprobe)).

### The service does not start with the session

The units are `WantedBy=graphical-session.target`. A plain SSH login has no graphical session
and does not start them. Log in graphically or start them by hand. After
`systemctl --user enable`, log out and in once.

### uinput: permission denied (Wayland)

`BACKEND = auto` picks uinput under Wayland. The user needs write access to `/dev/uinput`.

1. The packages do not install the udev rule. Enable it yourself as described in
   [installation.md](installation.md#2-first-time-setup) and
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
| Backend | XTest | uinput (needs the opt-in udev rule) |
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

### `dofscroll-ui.service` exits at once and no window appears (Qt UI)

The Qt UI keeps a single-instance lock (`dofscroll-ui.lock`) in the per-user runtime directory
(`$XDG_RUNTIME_DIR`, usually `/run/user/<uid>/`; Windows and macOS: the per-user dofscroll runtime
folder). The directory is writable inside the unit sandbox (`ReadWritePaths=%t`) and is cleaned at
logout, so the lock never survives a reboot. A lock left by a killed instance is detected as stale
at the next start.

- "dofscroll-ui is already running" (exit 0): a live instance holds the lock; the second start asks
  it to show its window. If you see no window or tray icon, check
  `systemctl --user status dofscroll-ui.service dofscroll-tray.service` (only one of the two runs).
- "cannot create the single-instance lock ..." (exit 1): the runtime directory is not writable.
  Check `ls -ld "$XDG_RUNTIME_DIR"` and that a unit override has not removed `%t` from
  `ReadWritePaths`.
- `dofscroll-ui --help` and `--version` print and exit without starting a window.
- Versions up to 0.1.0-beta.1 kept the lock in `~/.local/share/dofscroll/`, which the unit mounts
  read-only: the unit exited with status 0 and no message. Update the package.

### Settings window or menus are in German (or another language)

The UI follows the locale (`LC_MESSAGES`, `LANGUAGE`). To force English:

    systemctl --user edit dofscroll-tray.service

and add

    [Service]
    Environment=LANGUAGE=C

or start the tray by hand with `LANGUAGE=C dofscroll-tray`. Daemon log lines are always English.
Translations are looked up in `$DOFSCROLL_LOCALEDIR`, `../share/locale` next to the binary,
`/usr/local/share/locale` and `/usr/share/locale`. A missing catalog shows English.

### A button does nothing, or input arrives twice

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. Check the driver mapping first: a button that sends the same key or click everywhere is mapped in `/etc/spnavrc` (Linux) or the 3Dconnexion driver settings (Windows, macOS), see [modes.md](modes.md#buttons-and-the-vendor-driver). Double input usually means the driver also scrolls or pans in that program; turn that off per application, see [windows-mac.md](windows-mac.md).

### Scrolling is inverted or on the wrong axis

Set `<CH>_SIGN = -1` or change the `<CH>_MIX` line, or run "Learn axes" in the tray. If you changed
`spacenavd` settings (`/etc/spnavrc`), the axis meaning changed too.

### Keys stay pressed after a crash

The daemon releases Ctrl, arrows and the left mouse button at start, and the unit runs
`dofscroll --release-keys` after it stops. If a key still hangs, restart the service.

### A key or scrolling gets stuck

Symptoms: an arrow key, Ctrl or a mouse button seems to stay pressed, or scrolling continues
without the SpaceMouse being touched.

What the daemon does about it on its own:

- **Stale-input watchdog.** If the device state is older than 250 ms, an axis counts as
  released. Scrolling, zoom and arrow keys derived from axes stop within that time, even if
  the connection to `spacenavd` is lost while the puck is deflected.
- **Safety net.** If no axis is outside its dead zone and no button has been held for 250 ms
  (`SAFETY_QUIET_NS` in `src/daemon_core.hpp`), the daemon releases every key and mouse button
  it holds and clears the output's own bookkeeping. If something was still held, the journal
  shows `safety release: ...` (at most once per 10 s). A button you hold on purpose
  (click-and-hold, a key bound to a button) is never released by the net.
- A device list change, a profile or program change, a config reload, a mode switch, `ENABLED=0`
  and the tray's axis display (output pause) all release what is held.

If a key still hangs, recover in this order:

1. Tray menu: **Release all keys**. It runs `dofscroll --release-keys`.
2. In a terminal: `dofscroll --release-keys`.
3. `systemctl --user restart dofscroll.service` (the unit also runs `--release-keys` when the
   daemon stops).

To report it, collect the log of the time around the incident
(`journalctl --user -u dofscroll.service --since "-15min"`) and look for `Device list changed`,
`Profile:`, `Output paused`, `safety release` and `Mode` lines. Note whether the daemon was
restarted after the last update: an old daemon binary keeps running until the service is
restarted.

### Scrolling happens in another user's session

The 3D mouse is one shared device, so a second logged-in user's daemon also receives its events.
Each daemon pauses its output while its own session is not the active one (status file
`session=inactive`, log `session inactive: output paused`). If scrolling still reaches the wrong
session, check the journal of both users for that line. On Linux the session is found through
`XDG_SESSION_ID` and `/run/systemd/sessions/<id>`: if the variable is missing (for example a
service started outside the login session) or the file is unreadable, the daemon assumes it is
active. A locked screen is not detected on Windows and macOS. See
[security.md](security.md#12-several-users-on-one-pc).

### Config changes do not apply

The daemon reloads when the file changes. Force it: `systemctl --user kill -s HUP dofscroll.service`.
Check the journal for a rejected line. `BACKEND` needs a restart. Validate a file with
`build/tests/check_config FILE` (source build only).

### Duplicate input in a 3D program

Both dofscroll and the program read the device. Add `[app:<WM_CLASS>]` with `ENABLED = 0`.

## Reporting a problem

Include the dofscroll version (`dofscroll --version`), distribution, X11 or Wayland, the device
name from the journal, and the relevant journal lines. Remove personal data from the logs first.
