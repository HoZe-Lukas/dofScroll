<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release notes

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Releases and their generated notes: https://github.com/HoZe-Lukas/dofScroll/releases. Versions `0.x` are marked pre-release.

## 1.2.0

Feature release. One **Restart** item in the tray and on the Overview restarts the daemon and the interface together; the motion chips tell push, tilt and twist apart and show the keys of a key action; the new settings `ZOOM_MODIFIER` (the keys held while zooming, per profile, for example Cmd on macOS) and `INVERT_AXES` (flip the direction of single axes in every joystick mode) can be set per profile; the interface starts `systemctl` and `launchctl` only from fixed directories; the CI images, the Qt installer and the release artifacts are pinned and attested. See [docs/release-notes-1.2.0.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-1.2.0.md) and [Configuration reference](Configuration-reference).

## 1.1.2

Interface release. A joystick mode shows what each of the six motions of the cap does (vertical push, vertical tilt, horizontal push, horizontal tilt, twist, lift / press) as six symbols in the list and six cards in the detail view, and the same look is used on the Joystick modes page, in the profile's General tab, on the Overview cards, on the Button modes page and on the Buttons tab. The built-in joystick modes and button modes explain themselves in plain words, you can choose which joystick modes Next and Previous joystick mode go through, the output switch is the first box of a profile, and the "Extensions" page is called "User modes" (and recognises files that older versions copied). Full notes: [docs/release-notes-1.1.2.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-1.1.2.md).

## 1.1.1

Fix release for 1.1.0: upgrading the RPM or DEB now restarts the daemon and the interface also for domain accounts (user names with an `@`) and for an interface that the desktop's session restore started; in 1.1.0 both kept running the old program after `dnf install` for such users. Full notes: [docs/release-notes-1.1.1.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-1.1.1.md).

## 1.1.0

Tested extensively only on RHEL 9 (X11, KDE Plasma, SpaceMouse Pro and Compact); other Linux distributions
install-tested in CI containers; Windows and macOS built by CI, never run with a real device. New: several
SpaceMice side by side with their own profile and joystick mode (Windows and macOS direct HID; tests with a fake
source only, Linux still merges the devices), five new joystick modes (`sheet`, `photo`, `zoom-pan`, `vertical-only`,
`fast`), and the interface stays in the tray after login on KDE. A 1.0.1 was prepared but never published; its
changes are included: the Linux packages
(RPM, DEB, TGZ) now contain their own Qt 6.9.3 (the same version as Windows and macOS) and install on RHEL 9 with
any Qt 6 or none, instead of requiring Qt 6.10 from the system. Windows and macOS: new key `INPUT`
(`auto`, `driver`, `hid`) to read the device through the official 3Dconnexion driver or directly, `dofscroll-hidprobe`
and a guided coexistence test (unverified on real hardware). Interface: split push and tilt per channel in the
joystick mode editor, accessibility work, warning for lift/press in an axis mix. Full notes:
[docs/release-notes-1.1.0.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-1.1.0.md).

## 1.0.0

Tested extensively only on RHEL 9 (X11, KDE Plasma, SpaceMouse Pro and Compact); other Linux distributions
install-tested in CI containers; Windows and macOS built by CI, never run with a real device. New since 0.1.1:
a new symbol (app icon, tray icon, macOS template) and the website in English and German. Full notes:
[docs/release-notes-1.0.0.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-1.0.0.md).

## 0.1.1 (pre-release, as every 0.x version)

The Qt interface is the only tray and settings window (GTK tray removed), profile-centric interface with
autosave and visible inheritance, built-in joystick and button modes, choose what a button cycles through
(`CYCLE`), split push and tilt per mode, key chords on buttons, new wiki pages for modes and requirements;
full notes: [docs/release-notes-0.1.1.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-0.1.1.md).

## 0.1.0-beta.2 (pre-release)

Fix release for beta.1; full notes: [docs/release-notes-0.1.0-beta.2.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-0.1.0-beta.2.md).

- Fixed: `dofscroll-ui.service` exited immediately under systemd (lock file in a read-only path), so start at login did not work with the Qt interface on Linux
- `dofscroll-ui --help` prints the usage instead of starting a second instance
- Code of conduct, install smoke test for the Linux packages in CI
- Added: tray items "Restart daemon" and "Restart interface"; `dofscroll-ui.service` pulls in the daemon (`Wants=`); the Windows installer stops and restarts the interface on update; Linux packages restart running user units on upgrade (best effort). See [Installation](Installation)

## 0.1.0-beta.1 (pre-release)

Summary of everything on `main` since `v0.1.0-rc2`; the full draft is [docs/release-notes-0.1.0-beta.1.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/release-notes-0.1.0-beta.1.md). **Tested on hardware only on:** RHEL 9, X11, KDE Plasma, SpaceMouse Pro and Compact. Windows and macOS are built by CI and were not run on hardware.

- Qt6 interface `dofscroll-ui` (Channels, General, Axes, Modes, Profiles, running-program picker, tray), see [User interface](User-interface)
- Start at login on Linux, Windows and macOS
- Linux packages with the Qt interface (GTK fallback); unsigned Windows installer and ZIP, unsigned macOS DMG (SmartScreen and Gatekeeper notes in [Installation](Installation))
- Stuck-key safety net and tray "Release all keys" (both trays)
- Output pauses while the own session is not the active one
- Documentation: dofscroll complements the vendor driver, UTF-8 policy

Known limitations: see the draft notes and the [Roadmap](Roadmap).

## 0.1 candidates (pre-release)

Tags `v0.1.0-rc1` and `v0.1.0-rc2`; the GitHub pre-release is `0.1.0-rc2`. **Not a stable release.** The summary below is derived from the git history up to `v0.1.0-rc2`. The current state of `main` is newer, see [Roadmap](Roadmap).

**Tested only on:** RHEL 9, X11, KDE Plasma, SpaceMouse Pro and Compact. Everything else is built in CI but not run on hardware.

Added:
- Modes as plain files (`~/.config/dofscroll/modes/`), built-in `normal`, `off` and `timeline`, example modes, `BUTTON_<n>` actions including `mode:<id>` and `mode:next`, every key and mouse buttons 1-3 on X11 and uinput
- First-start seeding of the config and example modes by the tray (published atomically)
- Tray: application modes list, quick "Mode" menu, German translation, translatable axis-wizard messages
- Device database with factory button counts, `devices/*.conf` HID descriptions and the HID report decoder
- Platform interfaces and a platform-independent daemon core with a thin Linux main
- CMake build (targets `check-fast`, `check`, `check-deps`, `install-user-links`, ...), CPack packages (RPM, DEB, TGZ), release workflow with checksums
- CI with `-Werror`, sanitizers, cppcheck, secret scan and pinned actions

Changed:
- Project renamed from `spnav-scroll` to `dofscroll` (old config paths are still read)
- `BUTTON_<n>` is 1-based; `plus` is the main-keyboard plus and `kp_plus`/`kp_minus` were added
- Application profiles now take precedence over the active mode's overrides
- Documentation and code comments are English

Fixed:
- X11 key tap on held keys, device-add event, early SIGHUP handling, HID descriptor parsing

## After 0.1.0-rc2 (detail, now part of 0.1.0-beta.1)

- Design principle made explicit: dofscroll complements the vendor driver instead of duplicating it: a button mapping that is the same everywhere and always (for example a button that sends Esc) is configured in the driver (spacenavd's spnavrc on Linux, the 3Dconnexion driver on Windows and macOS); dofscroll's own button actions are for bindings that depend on dofscroll's state: they apply only in a mode or profile, switch or toggle modes and behavior, or work together with the axis handling; dofscroll also handles what the driver does not: shaping scroll and zoom from the six axes, keys derived from axes, and per-program and per-device profiles. The shipped examples no longer bind always-the-same keys or clicks to buttons (the features stay; mode-specific bindings remain).

- Windows and macOS daemons (CI-verified, **hardware-unverified**), see [Installation](Installation)
- `dofscroll-hidprobe` diagnostic tool, see [Devices](Devices)
- Security review and fixes: units rendered with the final prefix, opt-in udev rule, mode-file safety notes, load-time chord warnings, rejection of process-level overrides in mode files, log sanitizing, see [Security](Security)
- Tray: edit mode files, create editable copies of built-in modes, "Start at login" checkbox
