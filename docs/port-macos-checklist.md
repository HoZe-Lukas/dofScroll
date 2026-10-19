<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# macOS hardware test checklist

Everything in `port-macos.md` under "What is NOT verified" is tested here. Nothing on this list has
been done yet. Record macOS version, Mac model/architecture, SpaceMouse model and whether the
3Dconnexion driver was installed/running for every run. Logs: `~/Library/Caches/dofscroll/dofscroll.log`
(`dofscroll --debug` in a terminal after stopping the agent prints the evaluated channels).

Setup: build (`cmake -S . -B build && cmake --build build -j4`), run `build/dofscroll` once from a
terminal, then continue with the permission step.

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

- [ ] Editing `dofscroll.conf` is picked up automatically.
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

- [ ] 3Dconnexion driver installed and running: does dofscroll receive reports? Do events double?
- [ ] Driver quit: dofscroll alone works.
- [ ] Driver per-app navigation switched off: no doubled input.
