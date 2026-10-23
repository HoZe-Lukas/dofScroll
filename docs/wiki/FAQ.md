<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# FAQ

**Which setup has actually been tested?**
RHEL 9, X11, KDE Plasma, with a SpaceMouse Pro and a SpaceMouse Compact. Everything else is built in CI but not run on hardware, see [Home](Home).

**Scrolling goes the wrong way. How do I reverse it?**
Reverse a single motion: on the Channels tab of the profile, "Invert direction of" has a check box per motion (`INVERT_AXES`; push and tilt are separate, so you can flip only the one that feels wrong). It works for every joystick mode and can be set per program or device. In the joystick mode editor, "Reverse direction" of a motion writes the sign of that channel instead. See [Configuration reference](Configuration-reference).

**Does it work on Wayland?**
There is a uinput backend that `BACKEND = auto` picks under Wayland, but it is untested. It needs write access to `/dev/uinput` through a udev rule you install yourself, and program profiles work only with `kdotool` on KDE. See [Troubleshooting](Troubleshooting).

**Does dofscroll use the 3Dconnexion driver? Do I have to stop it?**
No, never stop it: the official driver keeps running for your CAD programs. On Windows and macOS dofscroll reads the SpaceMouse through the installed driver's programming interface when the driver is installed, and directly as raw HID otherwise (setting `INPUT = auto | driver | hid`, default `auto`; the interface shows which one is active). dofscroll contains nothing of the driver: it loads the installed driver's interface at run time, with declarations written from public knowledge. The driver, its interface and the names 3Dconnexion and SpaceMouse belong to 3Dconnexion (a Logitech company); dofscroll is not affiliated with or endorsed by them, and no permission was requested from them. This path has only been tested against a fake driver, never the real one. On Linux, `spacenavd` is the driver and `INPUT` is ignored. See [Requirements](Requirements) and `docs/legal-notes.md`.

**Why X11 first?**
That is the maintainer's setup, the only one that can be verified on hardware today. XTest is simple and needs no special permissions; the uinput backend needs an opt-in rule because `/dev/uinput` works below the display server (see [Security](Security)).

**Does it conflict with the vendor driver or other programs?**
On Linux, `spacenavd` applies its own sensitivity, dead zone and axis settings first; keep it at defaults and tune in dofscroll. Programs that read the device themselves (Blender, CAD) get input twice unless you add `[app:<name>]` with `ENABLED = 0`. On Windows and macOS the official 3Dconnexion driver can stay installed; see the next question. The proprietary 3Dconnexion SDK is not shipped or linked. See [Profiles](Profiles).

**Can I keep the official 3Dconnexion driver installed? Do I need it?**
You do not need it for dofscroll, and you need it for your CAD programs, so keep it installed and running: dofscroll works next to it and never asks you to stop it. On Windows and macOS dofscroll reads the SpaceMouse through the installed driver's interface when the driver is installed, and directly otherwise (setting `INPUT`, default `auto`). On Linux there is no vendor driver (`spacenavd` takes its place). On Windows and macOS, whether both really run at the same time is not verified on real machines yet and is the top priority: the guided test finds out (about 5 minutes, it changes nothing and never stops the driver) and writes one file for issue #36 (Windows) or #37 (macOS). How to run it: [Requirements](Requirements). Community reports say Windows probably works and macOS may not; those are unconfirmed.

**A program scrolls twice (driver and dofscroll). Can that be switched off for many programs at once?**
Yes, on Windows: Settings > General > **Remove driver axis settings...** lists the programs with active driver axes. Tick the ones to clean up (3D and CAD programs are not ticked by default), and the driver's axes are switched off there, with a backup you can restore. See [User interface](User-interface#driver-cleanup-windows).

**Can I use two SpaceMice?**
On Linux through `spacenavd` they arrive as one merged stream, so `[device:*]` profiles apply to both and per-device joystick modes are not possible. The Windows and macOS ports read each device separately: every device then has its own profile, joystick mode, holds and latches, and only one device per model is used (a Pro next to a Compact, not two Compacts). That is covered by tests but unverified on hardware. Details: [docs/multi-device.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/multi-device.md).

**Is it safe to share or download joystick modes?**
Treat a joystick mode file like a script: it can press any key on the key list, including modifier chords, while a button is held or an axis is deflected. Read it first (look for `key:`, `keys:`, `zoom`, `mode:`), and when you share one, say which keys it presses. See [Joystick modes](Modes).

**Does it run as root?**
No. The daemon and tray run as your user in systemd user units. Only enabling `spacenavd` and the optional udev rule need root.

**A key or scrolling seems stuck. What now?**
The daemon has a stale-input watchdog and a safety net that releases held keys and buttons when the device is quiet. If something still hangs: tray menu "Release all keys", or `dofscroll --release-keys` in a terminal, or restart the service. See [Troubleshooting](Troubleshooting).

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
Yes: an unsigned Windows installer and ZIP and an unsigned, not notarized macOS DMG (Apple silicon) are attached to every release. They are built and unit-tested by CI; on Windows the basic functions work on one real machine (first tests), macOS has never been run with a real device, see [Supported systems and requirements](Requirements) and the [Installation](Installation) page for the SmartScreen and Gatekeeper steps.

**A key got stuck. What now?**
Run `dofscroll --release-keys` or restart the service. This can happen after SIGKILL under X11, a known limitation, see [Security](Security).

**Can two users share one PC with a 3D mouse?**
Yes: each user runs their own daemon and a daemon pauses its output while its own session is not the active one (fast user switching, remote desktop); this is untested with a second real session, see [Security](Security).

**How can I help verify a device?**
Run `dofscroll-hidprobe --guided` and attach the report to an issue, see [Devices](Devices).
