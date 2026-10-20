<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# FAQ

**Which setup has actually been tested?**
RHEL 9, X11, KDE Plasma, with a SpaceMouse Pro and a SpaceMouse Compact. Everything else is built in CI but not run on hardware, see [Home](Home).

**Does it work on Wayland?**
There is a uinput backend that `BACKEND = auto` picks under Wayland, but it is untested. It needs write access to `/dev/uinput` through a udev rule you install yourself, and program profiles work only with `kdotool` on KDE. See [Troubleshooting](Troubleshooting).

**Why X11 first?**
That is the maintainer's setup, the only one that can be verified on hardware today. XTest is simple and needs no special permissions; the uinput backend needs an opt-in rule because `/dev/uinput` works below the display server (see [Security](Security)).

**Does it conflict with the vendor driver or other programs?**
On Linux, `spacenavd` applies its own sensitivity, dead zone and axis settings first; keep it at defaults and tune in dofscroll. Programs that read the device themselves (Blender, CAD) get input twice unless you add `[app:<name>]` with `ENABLED = 0`. On Windows, disable the 3Dconnexion driver's per-application navigation for programs you test. The proprietary 3Dconnexion SDK is not used. See [Profiles](Profiles).

**Can I use two SpaceMice?**
On Linux through `spacenavd` they arrive as one merged stream, so `[device:*]` profiles apply to both and per-device joystick modes are not possible. The Windows and macOS ports read each device separately, but that is unverified on hardware.

**Is it safe to share or download joystick modes?**
Treat a joystick mode file like a script: it can press any key on the key list, including modifier chords, while a button is held or an axis is deflected. Read it first (look for `key:`, `keys:`, `zoom`, `mode:`), and when you share one, say which keys it presses. See [Joystick modes](Modes).

**Does it run as root?**
No. The daemon and tray run as your user in systemd user units. Only enabling `spacenavd` and the optional udev rule need root.

**A key or scrolling seems stuck. What now?**
The daemon has a stale-input watchdog and a safety net that releases held keys and buttons when the device is quiet. If something still hangs: tray menu "Release all keys" (both trays), or `dofscroll --release-keys` in a terminal, or restart the service. See [Troubleshooting](Troubleshooting).

**What happened to the GTK tray?**
It was removed in 0.1.1: the Qt interface (`dofscroll-ui`) is the only tray and settings window on Linux, Windows and macOS. A package upgrade moves an enabled `dofscroll-tray.service` to `dofscroll-ui.service`. See [User interface](User-interface).

**Which devices are supported?**
On Linux, models `spacenavd` supports. Factory button counts are known for the six models on [Buttons](Buttons); only Pro and Compact are tested.

**Should I map a button to Esc, Enter or a click in dofscroll or in the driver?**
If it is the same everywhere and always, set it in the driver: it works in every program and survives dofscroll being stopped. If it should apply only in a certain joystick mode or profile (Space as play/pause in `timeline`), bind it in dofscroll (`key:`, `click`). dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a joystick mode or profile, switch or toggle joystick modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. See [Buttons](Buttons).

**How many buttons can I bind?**
Up to the factory count of your model (2 for Compact, 15 for Pro), numbered from 1. See [Buttons](Buttons).

**Where are the logs?**
`journalctl --user -u dofscroll.service` (and `dofscroll-ui.service`). On Windows `%LOCALAPPDATA%\dofscroll\dofscroll.log`. Run `dofscroll --debug` for button indices and live axes. See [Troubleshooting](Troubleshooting).

**How do I uninstall?**
Disable the user units, remove the package, and delete the udev rule if you added it; your config stays until you delete `~/.config/dofscroll`. See [Installation](Installation).

**Why do scrolling and zoom feel different from the tray defaults?**
The seeded config differs from the built-in defaults and `spacenavd` settings affect what dofscroll sees. Tune sensitivity, dead zone and curve per channel, see [Configuration reference](Configuration-reference).

**Is there a Windows or macOS installer?**
Not yet. Both ports exist as CI-built daemons configured by file and have not been run on hardware; installers and a Qt interface are planned, see [Roadmap](Roadmap).

**A key got stuck. What now?**
Run `dofscroll --release-keys` or restart the service. This can happen after SIGKILL under X11, a known limitation, see [Security](Security).

**Can two users share one PC with a 3D mouse?**
Yes: each user runs their own daemon and a daemon pauses its output while its own session is not the active one (fast user switching, remote desktop); this is untested with a second real session, see [Security](Security).

**How can I help verify a device?**
Run `dofscroll-hidprobe --guided` and attach the report to an issue, see [Devices](Devices).
