<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# macOS hardware test checklist

Everything in `port-macos.md` under "What is NOT verified" is tested here. Nothing on this list has
been done yet. Record macOS version, Mac model/architecture, SpaceMouse model and whether the
3Dconnexion driver was installed/running for every run. Logs: `~/Library/Caches/dofscroll/dofscroll.log`
(`dofscroll --debug` in a terminal after stopping the agent prints the evaluated channels).

Setup: build (`cmake -S . -B build && cmake --build build -j4`), run `build/dofscroll` once from a
terminal, then continue with the permission step.

## 0. Does dofscroll run next to the official 3Dconnexion driver? (do this first, about 5 minutes)

Policy: the official driver stays installed and running (CAD programs need it); dofscroll is designed
to work next to it. If both cannot read the device at the same time, that is a bug of dofscroll to be
fixed. Nothing about this has been verified on hardware yet. The test changes nothing and never stops
the driver:

    sh /Applications/dofscroll.app/Contents/Resources/diagnostics/test-driver-coexistence.sh

(the source tree has it as `scripts/test-driver-coexistence.sh`; Terminal needs the Input Monitoring
permission for `dofscroll-hidprobe`).

- [ ] The script ran to the end and wrote `dofscroll-driver-test-report.txt`; attached to issue #37.
- [ ] Test A (driver running): the report says whether the device opened and whether reports arrived.
- [ ] Test B (driver and dofscroll together): scrolling once, not twice; the CAD program still reacts
      normally; dofscroll reacts.
- [ ] Test C is optional and for diagnosis only (you quit the driver yourself, the script never does).

## 1. Permissions

- [ ] First start shows an Input Monitoring prompt or the daemon logs that the permission is
      missing (write down the wording and macOS version).
- [ ] After granting Input Monitoring: the log reports the device(s).
- [ ] Without Accessibility: the daemon logs it and stays alive; no events are posted.
- [ ] After granting Accessibility (restart the daemon): events are posted.
- [ ] Revoking a permission while running: no crash, a log line, recovery after re-granting.
- [ ] After rebuilding the binary the old grant no longer works (expected); re-adding fixes it.

## 2. Device detection (per model you own)

- [ ] SpaceMouse Compact / Wireless / Pro / Pro Wireless / Enterprise / SpaceNavigator: name in the
      log matches `devices/*.conf`, vendor/product ids are right, button count equals the factory
      count (2 / 2 / 15 / 15 / 31 / 2).
- [ ] Two devices at once: two `Device:` lines, separate profiles apply (`[device:NAME]`).
- [ ] Two SpaceMice, one lane each (docs/multi-device.md): a button on device B switches only B's joystick mode; zoom on both keeps Ctrl until the last one stops; unplugging one leaves the other working; a second device of the same model is ignored with a log line; the status line lists both devices.
- [ ] Wireless models: via the receiver and via cable.

## 3. Motion and buttons

- [ ] Each of the six axes alone: log (`--debug`) shows exactly one axis moving; note which
      physical motion maps to which axis and whether the sign is right (fix with a negative
      `scale` in the device description).
- [ ] Release returns to zero; no drift at rest.
- [ ] Every button 1..N produces `BUTTON_<n>` press and release; bit order matches the labels.
- [ ] Unplug while a button is held: the button is released, no stuck input.

## 4. Output in real apps

- [ ] Vertical and horizontal scroll in Safari, Chrome, Preview, Finder, Xcode: direction and
      speed plausible (compare with the trackpad's natural-scrolling setting).
- [ ] Zoom (Ctrl held + scroll) in the same apps: which of them zoom, which do not (note them).
- [ ] Nav mode arrow keys in a text editor and in Preview.
- [ ] Click / right-click / middle-click buttons.
- [ ] Held Ctrl never stays stuck after quitting the daemon with SIGTERM, with `kill -9` and
      after unplugging the device (then `dofscroll --release-keys`).

## 5. Profiles

- [ ] `[app:com.apple.Safari]` section overrides apply only while Safari is frontmost; switching
      apps switches the profile within about a second (log line `Profile:`).
- [ ] An app without a bundle identifier falls back to the localized name.
- [ ] `mode:` button actions and `MODE` per app work as on Linux.

## 6. Reload

- [ ] Editing `dofscroll.conf` or a profile file in `devices/` or `apps/` is picked up automatically.
- [ ] `requestReload()` (tray, once W3 exists) or a small test client reloads the config; a second
      request after the reload still works; no daemon running gives a clear error.

## 7. LaunchAgent

- [ ] `install-launchagent.sh` installs and starts the agent; `launchctl print gui/$(id -u)/io.github.dofscroll.daemon` shows it running.
- [ ] After logout/login the daemon runs without a terminal and the permissions still apply.
- [ ] `kill -9` of the daemon: launchd restarts it after about 10 s; a clean exit is not restarted.
- [ ] Fast user switching: the agent of the other user does not interfere.
- [ ] `uninstall-launchagent.sh` stops it and removes the plist; running it twice is fine.

## 8. Lifecycle

- [ ] Hot-plug: unplug and replug repeatedly; the device returns without restarting the daemon.
- [ ] Sleep/wake (lid close, display sleep, Bluetooth models): input resumes; note the delay.
- [ ] Apple Silicon and Intel (if both are available): same results.

## 9. Vendor driver

- [ ] 3Dconnexion driver installed and running: does dofscroll receive reports? Do events double? (Section 0 answers this.)
- [ ] A program where events double: `ENABLED = 0` for it in dofscroll's `[app:...]` profile removes the double input.
- [ ] Diagnosis only (the tester quits the driver; never advice for normal users): driver quit, dofscroll alone works.

## 10. Driver interface path (`INPUT`, see `port-architecture.md`, "Driver interface backend")

Nothing of this path has run against the real driver; CI only runs it against a fake. Record the driver
version (3DxWare) and the macOS version for every item. Look at "Input: ..." on the Settings page,
`input=` in the status file and the log lines "Input: ...".

- [ ] **Background client (the key question):** driver installed and running, `INPUT = driver`, a browser
      or Preview in front (dofscroll's window is NOT in front): does the SpaceMouse scroll? Does it
      still scroll after switching to another program? If nothing arrives while another program is in
      front, record it: the driver path is then not usable for dofscroll on macOS.
- [ ] CAD program open (the driver serving it) at the same time: does the CAD program still work
      normally while dofscroll runs with `INPUT = driver`? Does registering as a client change its
      behavior (device taken from it, navigation stops, a different button behavior)? Quit dofscroll:
      is everything back to normal?
- [ ] `INPUT = hid` with the driver running: do reports arrive (public reports say the driver holds the
      device)? With the driver quit?
- [ ] `INPUT = auto` with the driver running and installed: which path is chosen ("Input: 3Dconnexion
      driver (automatic)"), does it work, does it stay (no switching back and forth in the log)?
- [ ] Axes: unit (full deflection should read about 350 in the live data of the Axes page; note the real
      value), directions and order against the Linux behavior, buttons: which `BUTTON_<n>` each
      physical button gives with the driver path (raw bits or nothing with a current driver).
- [ ] Device model and button count: the name in the status line (for a known model the name from
      `devices/`, otherwise "SpaceMouse (driver)").
- [ ] Driver installed but not running (quit its helper): `INPUT = auto` falls back to HID, `INPUT =
      driver` logs "refused the registration" and keeps retrying; start the driver: the next retry
      succeeds.
- [ ] Driver not installed at all: `auto` uses HID, `INPUT = driver` logs "not installed".
- [ ] Hot-plug of the device and sleep/wake with the driver path: the device returns, no stuck keys.
- [ ] Signed build: loading the vendor framework works (library validation); with the hardened
      runtime the entitlement `com.apple.security.cs.disable-library-validation` may be needed.
- [ ] The Settings page: the **Input path** drop-down, the "Active input" line and the About box (Qt
      and the 3Dconnexion note) look right; German text.
