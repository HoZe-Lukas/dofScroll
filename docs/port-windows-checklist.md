<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Windows: manual hardware test checklist

Nothing in the Windows port has been run on a real SpaceMouse. CI only proves that the code builds
and that the pure logic and the file operations pass their unit tests. A human has to tick every
box below (and write down the Windows version, the device model and whether the 3Dconnexion driver
was installed/running). Use the artifact `dofscroll-windows-*` of the CI run, or a local build.

Setup: copy `dofscroll.exe`, `devices\` and `examples\` into one folder. Start from a console with
`dofscroll.exe --version` first. The daemon has no console window; read
`%LOCALAPPDATA%\dofscroll\dofscroll.log` and `dofscroll-status`.

## 0. Does dofscroll run next to the official 3Dconnexion driver? (do this first, about 5 minutes)

Policy: the official driver stays installed and running (CAD programs need it); dofscroll is designed
to work next to it. If both cannot read the device at the same time, that is a bug of dofscroll to be
fixed. Nothing about this has been verified on hardware yet. The test changes nothing and never stops
the driver:

    powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1

(in the install folder or the unpacked ZIP; the source tree has it in `scripts\`; build tree: pass
`-ProbePath build\Release\dofscroll-hidprobe.exe`).

- [ ] The script ran to the end and wrote `dofscroll-driver-test-report.txt`; attached to issue #36.
- [ ] Test A (driver running): the report says whether the device opened and whether reports arrived.
- [ ] Test B (driver and dofscroll together): scrolling once, not twice; the CAD program still reacts
      normally; dofscroll reacts.
- [ ] Test C is optional and for diagnosis only (you stop the driver yourself, the script never does).

## 1. Start, files, paths

- [ ] `dofscroll.exe --version` prints a version and exits with code 0.
- [ ] Starting `dofscroll.exe` shows no console window; a process `dofscroll.exe` exists.
- [ ] `%LOCALAPPDATA%\dofscroll\dofscroll.log` and `dofscroll-status` appear; the status file is updated about every second and contains `device=`, `profile=`, `app=`, `mode=`.
- [ ] `%APPDATA%\dofscroll\` is the config folder (seeded or created by hand with `examples\default\windows\dofscroll.conf`; profiles are one file each in `devices\` and `apps\`, see `examples\split\`).
- [ ] The log contains no raw control characters or line breaks from device names.

## 2. Device detection (repeat per model: Compact, Wireless, Pro, Pro Wireless, Enterprise, SpaceNavigator)

- [ ] Device is listed in the log with the expected name and USB ids.
- [ ] Button count shown in the log/status equals the factory count (2 / 2 / 15 / 15 / 31 / 2), not the number of raw bits.
- [ ] Unplug and replug: log shows removal and re-detection; no stuck scrolling or stuck keys afterwards.
- [ ] Start the daemon with the device already plugged in, and with the device plugged in later.
- [ ] Two SpaceMice at once: two devices in the log with different ids; a `[device:NAME]` profile applies to the right one.
- [ ] Two SpaceMice, one lane each (docs/multi-device.md): a button on device B switches only B's joystick mode; zoom on both keeps Ctrl until the last one stops; unplugging one leaves the other working; a second device of the same model is ignored with a log line; the status line lists both devices.
- [ ] Wireless models: switching on/off and sleep of the mouse does not stop the daemon.

## 3. Motion (per axis)

For each axis, push/tilt/twist gently and fully; check direction and magnitude (`--debug` on the console build or the log):

- [ ] x (push right/left)
- [ ] y (lift/press)
- [ ] z (push forward/back)
- [ ] rx (tilt forward/back)
- [ ] ry (twist the cap)
- [ ] rz (tilt sideways)
- [ ] Full deflection is about +-350; no axis is inverted or swapped compared to Linux. Write down every deviation (fix: negative `scale` in `devices\*.conf`).
- [ ] No drift when the cap is released (motion returns to zero; no scrolling by itself).

## 4. Buttons

- [ ] Every button 1..n produces a press and a release event in the log (`--debug`), with the expected number (1-based `BUTTON_<n>`).
- [ ] Pro / Pro Wireless / Enterprise: raw bit order is a placeholder in `devices\*.conf`; record which physical button gives which number and report it (needed to fix the descriptions).
- [ ] Click button: left click while held; nav button: arrow-key mode; mode:next / mode:<id> switch the mode.
- [ ] Release of all buttons when the device is unplugged while a button is held.

## 5. Output

- [ ] Vertical scrolling in a web browser, Notepad, Explorer, Word/Excel (detent size feels right; `SCROLL_V_SENS` changes it).
- [ ] Horizontal scrolling (wheel_h) in Excel and a browser.
- [ ] Zoom (Ctrl + wheel) in a browser, an image viewer and Office; note any application where zoom does not work.
- [ ] Nav mode: arrow keys are sent (Notepad cursor movement) and Ctrl stays released afterwards.
- [ ] Mouse click / right / middle click via buttons.
- [ ] `key:<name>` actions: letters, F-keys, Enter, Esc, Space, Tab, plus/minus variants (`kp_plus`, `kp_minus`).
- [ ] Stop the daemon with a button/Ctrl held: afterwards no key or mouse button is stuck; `dofscroll.exe --release-keys` clears a stuck state.
- [ ] Elevated target window (e.g. an administrator console): note that injection is blocked (expected, UIPI); daemon keeps running.
- [ ] Lock screen / UAC prompt: no input arrives there (expected); after unlocking the daemon works again.

## 6. Per-app profile switching

- [ ] `[app:firefox.exe]` (or the exact executable name shown in the status file `app=` line) overrides a setting only in that program.
- [ ] Switching between two programs changes the profile within about a second (log line `Profile: ...`).
- [ ] Packaged/UWP app (e.g. Calculator, Photos): record which name appears (`ApplicationFrameHost.exe` or the real app).
- [ ] An elevated program in front: record what the app name shows (empty = default profile is expected).
- [ ] Mode precedence: global < device < mode < app (see `docs/configuration.md`).

## 7. Reload

- [ ] Edit `dofscroll.conf` or a profile file in `devices\` or `apps\` with an editor: the daemon reloads by itself within about a second (log line).
- [ ] Atomic save by another program (write temp file + rename) is detected.
- [ ] Edit a file in `modes\`: the mode list updates.
- [ ] Trigger the reload event (named event `requestReload()`; with the tray program once W3 exists): log shows "Reload requested".
- [ ] Invalid config line: warning in the log, previous settings stay active.

## 8. Autostart

- [ ] `packaging\windows\install-autostart.ps1 -ExePath <path>`: value appears under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- [ ] Log off and on (and restart): the daemon is running after logon, without a console window.
- [ ] Fast user switching: the daemon of user A does not read the device while user B is active, and works again after switching back (record behavior).
- [ ] `uninstall-autostart.ps1`: value gone, daemon no longer starts at logon.
- [ ] `stop-daemon.ps1` ends only the daemon of the current session.

## 9. Sleep / wake and long run

- [ ] Sleep and wake the PC (and hibernate): the device is detected again, no stuck scrolling or keys.
- [ ] Lock and unlock the session.
- [ ] Remote Desktop session: record whether the device is visible and whether output reaches the remote session.
- [ ] Leave it running for an hour of use: memory and CPU stay low (Task Manager), log file stays within its size limit.

## 10. Vendor driver interplay (3Dconnexion driver)

Run sections 2, 3 and 5 twice:

- [ ] Driver installed and running (default settings): does dofscroll still receive reports? Is the output doubled (driver and dofscroll both scroll)? (Section 0 answers this.)
- [ ] A program where the output is doubled: `ENABLED = 0` for it in dofscroll's `[app:...]` profile removes the double input.
- [ ] Diagnosis only (the tester stops the driver; never advice for normal users): driver background process exited, dofscroll alone.
- [ ] Record whether opening the device fails (log shows a Windows error) while the driver runs, and with which driver version.
- [ ] Driver not installed at all.

## Reporting

Attach: Windows version, device model(s) and firmware, driver state, `dofscroll.log`, and the lines of
this list that failed with what you saw.

## 11. Driver interface path (`INPUT`, see `port-architecture.md`, "Driver interface backend")

Nothing of this path has run against the real driver; CI only runs it against a fake COM server. Record
the driver version (3DxWare) and the Windows version for every item. Look at "Input: ..." on the
Settings page, `input=` in the status file and the log lines "Input: ...".

- [ ] **Driver running, a CAD program open and being used:** `INPUT = driver`: does dofscroll receive
      motion and buttons? Does the CAD program keep working normally (device not taken from it, no
      change of navigation or buttons)? Does a browser in front scroll (background client)?
- [ ] `INPUT = hid` with the driver running (public reports: both can read): reports arrive? Output
      doubled? Is opening the device refused (sharing violation in the log)?
- [ ] `INPUT = auto` with the driver installed and running: which path is chosen (default: direct HID
      first), does it work, does it stay (no switching back and forth in the log)? Then make HID
      impossible (driver holding the device exclusively, if it does that) and check that the driver
      path takes over.
- [ ] Driver installed but its service not running: `auto` and `hid` still work; `INPUT = driver` logs
      "cannot create the 3Dconnexion COM object" or "refused Connect"; start the driver: the next
      retry succeeds.
- [ ] Driver not installed at all: `auto` uses HID, `INPUT = driver` logs "not installed".
- [ ] Production registration checks accept the real installation: "3Dconnexion driver interface not
      usable: ..." must NOT appear. Record how the class is registered (native DLL under Program Files
      or a managed server with `mscoree.dll` and a code base), and where the files are.
- [ ] Units and numbering: full deflection of translation and rotation (note the numbers the live data
      shows; placeholders are factor 1.0), rotation direction and the meaning of Angle (radians or
      degrees), whether key codes start at 0 or 1, which `BUTTON_<n>` each physical button gives.
- [ ] Device model and button count: the name in the status line ("SpaceMouse (driver)" unless known)
      and the button count (`Keys`) against the real number of buttons.
- [ ] Unplug and replug with the driver path, sleep/wake: the device returns, no stuck keys or scrolling.
- [ ] A second program that uses the same COM object (the CAD program) is not disturbed by
      dofscroll's `Connect` / `Disconnect`; after quitting dofscroll its settings are unchanged.
- [ ] The Settings page: the **Input path** drop-down, the "Active input" line and the About box (Qt
      and the 3Dconnexion note) look right; German text.
