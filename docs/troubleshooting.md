<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Troubleshooting

## Quick checks

Open **dofscroll** from the start menu and look at the setup guide ("Show setup guide" on the
Overview shows it again): it checks live whether the daemon runs, whether the SpaceMouse is found and,
on macOS, the two permissions, and says what to do. On Linux:

    systemctl status spacenavd
    systemctl --user status dofscroll.service dofscroll-ui.service
    journalctl --user -u dofscroll.service -n 50

## Logs and status files

| What | Where |
|---|---|
| Daemon log | `journalctl --user -u dofscroll.service` (add `-f` to follow, `-n 50` for the last lines, `-b` for this boot) |
| Interface log | `journalctl --user -u dofscroll-ui.service` |
| Log copy for the interface | `$XDG_RUNTIME_DIR/dofscroll.log` (usually `/run/user/<uid>/`, at most 300 lines) |
| Status file | `$XDG_RUNTIME_DIR/dofscroll-status`, lines `key=value`: `profile`, `device`, `app`, `enabled`, `paused`, `devices_seen`, `input` (Windows and macOS: `driver` or `hid`), `updated`. Removed on clean exit. |
| Config | `~/.config/dofscroll/dofscroll.conf` and `~/.config/dofscroll/modes/` |

### Reading the journal

Lines carry the daemon's messages. Look for:

- the device name and button count after the SpaceMouse connects (you need the name for
  `[device:*]` sections);
- profile switches, with the matched device and app;
- "control disabled" and "active again" (`ENABLED = 0` or `MODE = off` in effect);
- warnings about skipped config lines or joystick mode files;
- connection retries to `spacenavd`.

For button indices and the live axis values, stop the service and run the daemon in a terminal:

    systemctl --user stop dofscroll.service
    dofscroll --debug

It prints the axes about ten times per second and every button press with its index. Start the
service again afterwards.

## Problems

### Nothing happens

The setup guide of the interface ("Show setup guide" on the Overview) shows the first failing check.
The interface starts the daemon by itself about 6 seconds after it starts when none runs; if the guide
says "could not be started", the log (`journalctl --user -u dofscroll.service`, or the log window) has
the reason. A second daemon exits at once with "another dofscroll daemon is already running": that is
the single-instance lock, not an error (stop the first one, or use it).

1. Is `spacenavd` running? (The Linux packages enable it themselves; the guide shows the command if it is off.) `systemctl status spacenavd`. If not:
   `sudo systemctl enable --now spacenavd`. The daemon starts without it and reconnects with a
   backoff of up to 5 s, so you need no restart.
2. Does `spacenavd` see the device? Check `journalctl -u spacenavd` and that the device is
   plugged in. `lsusb` should list a 3Dconnexion device. With two SpaceMice, `spacenavd` reports
   only one (see [multi-device.md](multi-device.md)).
3. Is the user unit running? `systemctl --user status dofscroll.service`. If it is
   "not found", run `systemctl --user daemon-reload` and check step 3 of the setup in
   [installation.md](installation.md#2-first-time-setup).
4. Does the unit fail with status 203/EXEC? The program in the unit's `ExecStart` does not
   exist. A package install points at `/usr/bin`, the symlink flow from the build tree at
   `/usr/local/bin` (see "Run from the build tree"). Check with `systemctl --user cat dofscroll.service`.
5. Is output paused or disabled? The status file shows `enabled=0` or `paused=1`. The daemon
   pauses while the interface's Axes page or the axis assistant is open. An `[app:*]` section with
   `ENABLED = 0`, or `MODE = off`, switches it off for that program.
6. Does another program grab the device (Blender, CAD)? Switch dofscroll off for it with
   `[app:<name>]` `ENABLED = 0`.
7. Does the device send reports at all? `dofscroll-hidprobe --dump` shows the raw reports (see
   [devices.md](devices.md#verifying-a-device-with-hidprobe)).

### The service does not start with the session

The units are `WantedBy=graphical-session.target`. A plain SSH login has no graphical session
and does not start them. Log in graphically or start them by hand. After
`systemctl --user enable`, log out and in once.

### `status=218/CAPABILITIES`: the daemon does not start (AppArmor)

`systemctl --user status dofscroll.service` shows `status=218/CAPABILITIES`, the journal says
`Failed to drop capabilities: Operation not permitted`, and the daemon never runs (the interface
then starts it as a plain process, so scrolling works while autostart is broken).

Distributions that confine unprivileged user namespaces with AppArmor
(`sysctl kernel.apparmor_restrict_unprivileged_userns` = 1, Ubuntu 24.10 and newer) deny the
capability calls the sandbox setup needs, so the unit fails **before** `ExecStart`. It is not
specific to dofscroll: a transient unit with the same hardening running `/bin/true` fails the same
way. The units therefore do not set `CapabilityBoundingSet=`, `ProtectKernelModules=`,
`ProtectKernelLogs=` and `ProtectClock=` ([security.md](security.md#3-systemd-hardening-of-the-user-units)).

To repair an installed older release by hand:

    systemctl --user edit dofscroll.service dofscroll-ui.service

comment out or delete those four lines under `[Service]`, then `systemctl --user daemon-reload`
and start the units again. On a system that allows the calls (RHEL 9), the four directives can be
added the same way; see [security.md](security.md#3-systemd-hardening-of-the-user-units).

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

### The settings window shows no content (regional format with a decimal comma)

Symptom: the settings window opens with its title bar but the content looks empty. The widgets
are there - a tooltip still appears when the pointer rests on an invisible control - the controls
sit at their platform default size (22 px instead of 32 to 34 px), and some page grows to several
thousand pixels. `dofscroll-ui --self-test` fails the target size check and prints page sizes such
as `QSpinBox "..." is 16 x 6451 px`.

Cause: a numeric format with a decimal comma. `locale | grep LC_NUMERIC` shows something like
`LC_NUMERIC=it_IT.UTF-8`. The style sheet lengths are em values, and the conversion used to write
`0,571em` there - Qt cannot parse that, so the affected rules lose their padding and their size
constraints.

The conversion is locale-independent now. For an older release, start the interface with the C
numeric format (the language of the interface stays the one of the environment):

    LC_NUMERIC=C.UTF-8 dofscroll-ui

### Wayland versus X11

| | X11 | Wayland |
|---|---|---|
| Backend | XTest | uinput (needs the opt-in udev rule) |
| `[app:*]` profiles | Yes (`WM_CLASS`) | Only with `kdotool` on KDE. Native Wayland windows may not be found. |

Without an active-window query only `[global]` and `[device:*]` apply. To force a backend, set
`BACKEND = x11` or `uinput` in `[global]` and restart the daemon. The log says which backend
was chosen and why.

`dofscroll-ui --self-test` behaves differently on the two: on Wayland it skips the two info
popover checks (the popover opens, and its content), because a grabbing popup needs an input
serial from the compositor and a test never receives real input
(`qt.qpa.wayland: Failed to create grabbing popup`). X11 and the offscreen platform (CI) run both
checks in full. Everything else runs on every platform, but on a real Wayland session the self-test can still
fail afterwards at the `default size self-test` (the settings page needs more height than the allowed share of the
screen with some fonts); that check is font-dependent and is tracked in issue #193.

### The window opens large after every login

Older builds were also restarted by the desktop's session restore (KDE), which raced with the
`dofscroll-ui.service` unit: the second start asked the first to show its window. The interface now opts out
of session restore (`RestartNever`) and a start with `-session` never raises the window. If it still happens,
remove a `dofscroll` entry from the desktop's saved session (KDE: System Settings, Session, Desktop Session,
"Start with an empty session") and check `~/.config/autostart` for a second start of `dofscroll-ui`.

### Tray icon is missing

The tray icon of `dofscroll-ui` uses the StatusNotifierItem (AppIndicator) protocol.

- KDE Plasma: supported.
- GNOME: install and enable the AppIndicator extension (on RHEL:
  `gnome-shell-extension-appindicator`; the package name on other distributions is unverified).
- Check `journalctl --user -u dofscroll-ui.service` for errors.
- Without a tray (for example GNOME without the AppIndicator extension) dofscroll-ui opens its
  window minimized; restore it from the taskbar or the window list.
- A package built without Qt6 (a build from source without the Qt development packages) has no tray and no
  settings window. Edit the config by hand. The release packages always contain the interface and their own Qt.
- The tray icon and the look cannot be checked headless (`--self-test` reports `tray-available=0`).
  The bundled Qt shows the tray through the StatusNotifierItem protocol (KDE Plasma natively, GNOME with the
  AppIndicator extension) and uses its own Fusion style with the light/dark scheme of the desktop portal. If the
  window looks wrong, `dofscroll-ui --version` shows the Qt in use and `QT_QPA_PLATFORM=xcb` or `wayland`
  switches the platform plugin.
- `ldd /usr/bin/dofscroll-ui | grep -E 'Qt|not found'` must show only paths below `/usr/lib/dofscroll/lib/`.
  A "not found" means that a library that the package should bring is missing (report it with the output).

The daemon works without the tray. The tray is only needed for the settings window and the first
start seeding.

### `dofscroll-ui.service` exits at once and no window appears (Qt UI)

The Qt UI keeps a single-instance lock (`dofscroll-ui.lock`) in the per-user runtime directory
(`$XDG_RUNTIME_DIR`, usually `/run/user/<uid>/`; Windows and macOS: the per-user dofscroll runtime
folder). The directory is writable inside the unit sandbox (`ReadWritePaths=%t`) and is cleaned at
logout, so the lock never survives a reboot. A lock left by a killed instance is detected as stale
at the next start.

- No window at start is normal: the interface starts minimized to the tray. Click the tray icon,
  then "Settings...", to show it. Without a system tray the window opens minimized; restore it
  from the taskbar.
- "dofscroll-ui is already running" (exit 0): a live instance holds the lock; the second start asks
  it to show its window. If you see no window or tray icon, check
  `systemctl --user status dofscroll-ui.service`.
- "cannot create the single-instance lock ..." (exit 1): the runtime directory is not writable.
  Check `ls -ld "$XDG_RUNTIME_DIR"` and that a unit override has not removed `%t` from
  `ReadWritePaths`.
- `dofscroll-ui --help` and `--version` print and exit without starting a window.
- Versions up to 0.1.0-beta.1 kept the lock in `~/.local/share/dofscroll/`, which the unit mounts
  read-only: the unit exited with status 0 and no message. Update the package.

### Settings window or menus are in German (or another language)

The UI follows the locale (`LC_MESSAGES`, `LANGUAGE`). To force English:

    systemctl --user edit dofscroll-ui.service

and add

    [Service]
    Environment=LANGUAGE=C

or start the interface by hand with `LANGUAGE=C dofscroll-ui`. Daemon log lines are always English.
Translations are looked up in `$DOFSCROLL_LOCALEDIR`, `../share/locale` next to the binary,
`/usr/local/share/locale` and `/usr/share/locale`. A missing catalog shows English.

### A button does nothing, or input arrives twice

dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. Check the driver mapping first: a button that sends the same key or click everywhere is mapped in `/etc/spnavrc` (Linux) or the 3Dconnexion driver settings (Windows, macOS), see [modes.md](modes.md#buttons-and-the-vendor-driver). Double input usually means the driver also scrolls or pans in that program: see [The official 3Dconnexion driver and dofscroll](#the-official-3dconnexion-driver-and-dofscroll) below and [windows-mac.md](windows-mac.md).

### Scrolling is inverted or on the wrong axis

Set `<CH>_SIGN = -1` or change the `<CH>_MIX` line, or run the axis assistant ("Axis assistant..." on the Axes page of the interface). If you changed
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
- A device list change, a profile or program change, a config reload, a joystick mode switch, `ENABLED=0`
  and the tray's axis display (output pause) all release what is held.

If a key still hangs, recover in this order:

1. Tray menu: **Release all keys**. It runs `dofscroll --release-keys`.
2. In a terminal: `dofscroll --release-keys`.
3. Tray menu **Restart** (releases the keys, then restarts the daemon and the interface), or
   `systemctl --user restart dofscroll.service` (the unit also runs `--release-keys` when the
   daemon stops).

To report it, collect the log of the time around the incident
(`journalctl --user -u dofscroll.service --since "-15min"`) and look for `Device list changed`,
`Profile:`, `Output paused`, `safety release` and `Mode` lines. Note whether the daemon was
restarted after the last update: an old daemon binary keeps running until the service is
restarted (Linux packages try to do that on upgrade; see the upgrade section of
[installation.md](installation.md#upgrade)).

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

### macOS: the guide says a permission is missing

The daemon reports `permissions=missing:accessibility,input_monitoring` in the status file when macOS
does not accept its events. The setup guide shows one row for each (Input Monitoring: reading the
SpaceMouse; Accessibility: sending scroll, zoom and key events) with a button that opens the matching
pane of System Settings. Switch on **dofscroll** there; if it is not in the list, add
`/Applications/dofscroll.app/Contents/Resources/daemon/dofscroll` with the `+` button. The daemon looks
again every 5 seconds, so no restart is needed (the row turns to "Done"). The grants are tied to the
file and its code signature: after replacing the app or rebuilding an unsigned binary, switch it off and
on again. The pane names differ between macOS versions (unverified).

### Windows and macOS: nothing moves, or the wrong input path is active

The official 3Dconnexion driver may stay installed and running; dofscroll reads the device through the
driver's interface or directly (setting `INPUT`, see [configuration.md](configuration.md)). Look at the
line "Input: ..." in the interface (Overview and Settings page) or `input=` in the status file, and at
the log line "Input: 3Dconnexion driver (automatic)" or "Input not available: ...":

- "Input not available: ... not installed": `INPUT = driver` was chosen and no driver is installed.
  Install the driver or set `INPUT = auto` or `hid`.
- "3Dconnexion driver interface not usable: ...": the driver's library or COM class was found but
  refused (location not below Program Files or `/Library/Frameworks`, writable by others on macOS, a
  per-user override of the COM class, or the framework is missing). dofscroll never loads a library from
  anywhere else and no setting can change that.
- "refused the registration" (macOS) or "refused Connect" (Windows): the driver is installed but its
  service is not running or does not accept another client. Start the driver, or set `INPUT = hid`.
- The path opens but the device does nothing: with the driver path, try `INPUT = hid`; with the
  direct path (macOS: the driver may hold the device exclusively), try `INPUT = driver`. `auto` cannot
  tell "idle" from "blocked" and therefore stays on a path that opened; it only moves when its path
  has no device for about 20 seconds and the other path has one.
- Scrolling twice: the driver also scrolls for the program you test. Disable the driver's own
  navigation for that program in the driver's settings (do not stop the driver).

Which path works next to the driver is not verified on hardware yet; please report what you see
(model, OS version, driver version, `INPUT` value and the log lines above).

### Duplicate input in a 3D program

Both dofscroll and the program read the device. Add `[app:<WM_CLASS>]` with `ENABLED = 0`.

### The official 3Dconnexion driver and dofscroll

**Status: not verified on real hardware.** dofscroll is designed to run next to the official
3Dconnexion driver (3DxWare on Windows, 3Dconnexion Software on macOS): you can, and for your CAD
programs should, keep the driver installed and running. dofscroll does not need it. Whether both can
read the SpaceMouse at the same time on a real Windows or macOS machine has not been tried yet
(issues #36 and #37); that check is the top priority for the Windows and macOS ports. What is known so
far comes from community reports that are not official and not run by us (see
[the wiki page Requirements](https://github.com/HoZe-Lukas/dofScroll/wiki/Requirements)): on Windows both
readers probably work, possibly with double input if the driver also emits its own mouse or keyboard
events; on macOS the driver's helper may hold the device exclusively. On Linux there is no vendor
driver, `spacenavd` takes its place and this section does not apply.

If both cannot read the device at the same time on your system, **that is a bug of dofscroll to be
fixed**, not something you have to work around. Please report it with the test file described below.

Symptoms:

- **No scrolling at all, and the Overview page shows "SpaceMouse found but cannot be read"** (or the log
  has the same line): the device exists but opening it was refused. Windows: `CreateFile` failed with
  error 32 (sharing violation) or 5 (access denied); macOS: `IOHIDManagerOpen` returned
  `kIOReturnExclusiveAccess` (IOReturn 0xE00002C5). Another program holds the device exclusively.
- **No scrolling at all, and the Overview page shows "No input from the SpaceMouse yet"**: the device
  opened, but no report arrived in the 45 seconds after it was found. A SpaceMouse that nobody touches
  sends nothing, so move the cap first. If still nothing arrives, another program (possibly the driver)
  may keep the reports for itself. On macOS also check the Input Monitoring permission.
- **Scrolling happens twice in a program**: the driver and dofscroll both translate the motion. Set
  `ENABLED = 0` for that program in dofscroll's own profiles (the Profiles page, or an `[app:<name>]`
  section), so only the driver acts there; the CAD programs, where you want the driver, are the typical
  case. On Windows the **3Dconnexion driver** box on the General page lists the programs with active driver
  axes and switches them off for the ones you tick (with a backup), see [ui.md](ui.md#platform-notes-windows-and-macos).
  (Turning off the driver's own scroll and zoom for one particular 2D program in the driver's
  per-application settings by hand is possible as well.)
- **The CAD program stops reacting normally**: this must not happen. Report it with the test file.

The checks (about 5 minutes, nothing is changed, the driver is never stopped):

- Windows (from the install folder, or from the unpacked ZIP):
  `powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1`
- macOS: `sh /Applications/dofscroll.app/Contents/Resources/diagnostics/test-driver-coexistence.sh`

The script lists the SpaceMouse, finds out whether a 3Dconnexion driver is running, reads the device
with `dofscroll-hidprobe` while the driver runs (test A), starts dofscroll next to the driver and asks
you three yes/no questions (test B), and writes `dofscroll-driver-test-report.txt` in the current
folder. The report has no user name and no personal paths (the home folder is written as `~`); attach
it to issue #36 (Windows) or #37 (macOS). An optional test C repeats the reading with the driver
stopped. It is for diagnosis only, normal users do not need it; the script never stops the driver
itself. The tool can also be run by hand: `dofscroll-hidprobe --list` and `dofscroll-hidprobe --check`
(see [devices.md](devices.md#verifying-a-device-with-hidprobe)).

What the checks and messages are based on: the Windows daemon opens the device with
`FILE_SHARE_READ | FILE_SHARE_WRITE` and never exclusively; the macOS daemon uses `IOHIDManager` with
`kIOHIDOptionsTypeNone` and never the seize option. The message texts and the status line are unit
tested; the behavior of the real driver is not tested at all. If the test shows that the driver blocks
the second reader, the options are written down in
[port-architecture.md](port-architecture.md#if-the-driver-holds-the-device-exclusively).

## Reporting a problem

Include the dofscroll version (`dofscroll --version`), distribution, X11 or Wayland, the device
name from the journal, and the relevant journal lines. Remove personal data from the logs first.
