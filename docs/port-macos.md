<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# macOS port

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

Read `port-architecture.md` first. Hardware test list: `port-macos-checklist.md`. Install and
permissions: `packaging/macos/README.md`.

## Status

Experimental (work package W2). The backends are written against the platform seams of
`src/platform/platform.hpp` and compiled and unit-tested **only on the GitHub `macos-latest`
runner and on Linux** (see "What is verified"). **Nothing has been run with a real SpaceMouse on a
real Mac.** Every statement about device, event and permission behavior below is design intent or
documentation knowledge and is marked *unverified* until a human has done the checklist.

## Components

| Seam | macOS implementation | Where |
|---|---|---|
| `InputSource` | `IOHIDManager`, one `IOHIDDevice` per physical SpaceMouse; reports go through the shared `HidInputCore` and `EventQueue` | `src/platform/mac/mac_hid_input.*` |
| `Output` | `CGEventPost` (scroll wheel, keyboard, mouse buttons) | `src/platform/mac/mac_output.*`, key table in a header that is unit-tested on every OS |
| `ActiveWindowProvider` | `NSWorkspace.frontmostApplication` (Objective-C++), bundle identifier, falling back to the localized name | `src/platform/mac/*.mm` |
| `platformPaths()` | config `~/Library/Application Support/dofscroll`, runtime `~/Library/Caches/dofscroll` (rules in `src/platform/path_rules.hpp`) | `src/platform/mac/` |
| `ReloadListener` / `requestReload()` | user-only Unix domain socket in the runtime directory | `src/platform/mac/` |
| Autostart | per-user LaunchAgent | `packaging/macos/` |
| `main` | wires the above into `DaemonCore` like `src/dofscroll.cpp` does for Linux | `src/platform/mac/` |

The shared, OS-independent parts are unit-tested on every platform: path rules
(`test_path_rules`), report decoding and per-device state (`test_hid_input_core`,
`test_hid_descriptor`), the event queue (`test_event_queue`), the file-system layer (`test_fs`).

### Input: IOHID, no vendor SDK

`IOHIDManager` is matched on the vendor/product ids of the loaded `devices/*.conf` files, one
callback per input report. Each matched device becomes one `DeviceInfo` with its own id, so
per-device profiles and button counts work (unlike the merged stream of spacenavd on Linux).
Hot-plug comes from the matching/removal callbacks. The `3DconnexionClient` framework (part of the
proprietary SDK, incompatible with GPL-3.0-or-later) is not linked or shipped. The installed driver's
client interface can be used as a second input path, loaded at run time with `dlopen` (setting `INPUT`;
see `port-architecture.md`, "Driver interface backend", and `legal-notes.md`). A signed build with the
hardened runtime needs the entitlement `com.apple.security.cs.disable-library-validation` for it.

### Output: CGEvent

Scroll uses `CGEventCreateScrollWheelEvent`, keys `CGEventCreateKeyboardEvent` with virtual key
codes, mouse buttons `CGEventCreateMouseEvent` (button 2 = middle). `forceReleaseAll` posts key-up
and mouse-up for every key and button regardless of state, so a crashed earlier process cannot
leave a modifier stuck. Ctrl held for zoom is sent as a modifier flag on the scroll events.

### Active application

The bundle identifier (for example `com.apple.Safari`) is the string `[app:*]` sections are
matched against; apps without one fall back to the localized name. Profile patterns are matched
case-insensitively as substrings by the shared matcher.

### Running programs (picker)

`src/platform/mac/running_apps_mac.mm` lists `NSWorkspace.runningApplications` with activation
policy regular; the match string is the same lower-cased bundle identifier (or name) as the active
application provider. Called from a thread without a running `NSApplication` the list can be a
stale snapshot. Compile-checked and smoke-tested by `running_apps_selftest`; unverified on a
real desktop.

### Reload

The tray (or `requestReload()`) writes the config atomically and then connects to the daemon's
socket (`<runtime dir>/reload.sock`, mode 0600, directory 0700); the daemon reports one reload per
request and re-reads the file on its main thread. Reload is idempotent. There are no signals.

### Paths, logs, status

Config and modes: `~/Library/Application Support/dofscroll/{dofscroll.conf,devices/,apps/,modes/}`, same formats and layout
as on Linux. Runtime directory `~/Library/Caches/dofscroll` (user only): `dofscroll.log` (rolling,
same limits and sanitizing as Linux), `dofscroll-status`. launchd's own stdout/stderr capture is
switched off (`/dev/null`): it would never be rotated.

### LaunchAgent

`packaging/macos/install-launchagent.sh` renders `io.github.dofscroll.daemon.plist.in` into
`~/Library/LaunchAgents` (`RunAtLoad`, `KeepAlive` on crash only, `ProcessType Interactive`,
`LimitLoadToSessionType Aqua`) and bootstraps it into `gui/<uid>`. A LaunchDaemon (system level) is
not suitable: it has no GUI session and cannot post events to the user's session.

The UI's "Start at login" switch (`src/platform/mac/autostart_mac.cpp`, W18) writes the plists of
`io.github.dofscroll.daemon` and `io.github.dofscroll.ui` (paths XML-escaped, atomic, mode 0644) or
removes them; launchd loads them at the next login and nothing running is stopped. The
bootout / wait until gone / bootstrap sequence of the install script exists as `setAgents(...,
applyNow=true)` for installers (argument vectors, no shell). ctest `autostart_mac_render` renders
into a temporary directory only. Compile and runner test only, not tried on a real Mac.

## Permissions

- **Input Monitoring** is needed to read HID input (`IOHIDCheckAccess` / `IOHIDRequestAccess`).
- **Accessibility** is needed to post synthetic events (`AXIsProcessTrustedWithOptions`).
- Both are granted per binary in System Settings > Privacy & Security and are tied to the binary's
  path and code signature. Moving it, or rebuilding an unsigned binary, can invalidate the grant.
- The daemon is meant to detect a missing permission, write it to the log/status file and keep
  running instead of failing silently. Exact prompts and pane names per macOS version: *unverified*.
  The permissions are re-checked every 5 seconds (a revoke or grant while running is written to the
  status file at once and to the log, repeated at most every 10 minutes while something is missing).

## Exit paths and stuck keys

SIGTERM, SIGINT and SIGHUP end the main loop, which releases everything it pressed (clean path).
`std::terminate`, `exit()` and the system going to sleep (IOKit power notification) release through
a registry of the live outputs. After SIGKILL or a hard crash nothing can run: the next start
releases every key and button first (`--release-keys` does it on demand). Permanent errors (no
device descriptions, unusable output) exit with 78 (EX_CONFIG) in a terminal and with 0 under launchd,
because `KeepAlive/SuccessfulExit=false` would otherwise restart the daemon every 10 s forever.
Unit-tested: the registry with a fake output, the exit code and permission logic; compile-only on CI:
the sleep watcher, the permission watcher and the IOHID shutdown hand-over.

## Signing and notarization

Not done (work package W4). Unsigned local builds work for testing. For distribution: Developer ID
Application certificate, hardened runtime, `notarytool`, stapling; a stable signature also keeps
the privacy grants stable across updates. Needs a paid Apple developer account.

## What is verified

| Item | Evidence |
|---|---|
| Core, tests and macOS-specific files compile with Apple clang | `ports` workflow, job "macOS build + ctest" |
| Pure logic: path rules, HID report decoding, device state, event queue, key table, file-system layer | unit tests run by ctest on the runner |
| `plutil -lint` of the rendered LaunchAgent plist, shell scripts parse | `ports` workflow, macOS job |
| Daemon binary starts and prints its version | `ports` workflow smoke step (once the daemon target exists) |

## What is NOT verified (needs a human with a Mac and a SpaceMouse)

- That any of the supported models is detected and that the report layouts in `devices/*.conf`
  match the real hardware (also unverified on Windows and Linux).
- Whether `IOHIDManager` can read the device while the 3Dconnexion driver runs. The driver may
  seize the device exclusively; opening it from another process can then fail
  (`kIOReturnExclusiveAccess`, the daemon says so in its log and in the interface). dofscroll is
  meant to run next to the driver, so a conflict is a bug of dofscroll to be fixed, not something to
  work around by quitting the driver: `scripts/test-driver-coexistence.sh` (in the app under
  `Contents/Resources/diagnostics`) finds out and writes one report file for issue #37; the options
  for a conflict are in [port-architecture.md](port-architecture.md#if-the-driver-holds-the-device-exclusively).
- Motion, buttons, scroll and zoom output in real applications. Ctrl+scroll zoom may behave
  differently per app (macOS apps often use Cmd+scroll or pinch gestures).
- The Input Monitoring / Accessibility prompts and their wording per macOS version, behavior
  after the grant is revoked while running.
- Per-application profile switching by bundle identifier in real apps.
- LaunchAgent start at login, restart after a crash, behavior across fast user switching.
- Sleep/wake: whether devices re-appear and the daemon resumes without restart.
- Apple Silicon versus Intel (CI builds for the runner's architecture only).

## Vendor driver settings

The vendor driver (System Settings pane) can translate motion or buttons into events for
applications. It stays installed and running; dofscroll is designed to work next to it. Where input
doubles in a program, switch dofscroll off for that program (`ENABLED = 0` in an `[app:...]`
profile). See `windows-mac.md` for what the driver exposes and the reported lack of generic
scrolling in browsers, and [troubleshooting.md](troubleshooting.md#the-official-3dconnexion-driver-and-dofscroll).

## References

- IOHIDManager: https://developer.apple.com/documentation/iokit/iohidmanager_h
- Quartz Event Services (CGEvent): https://developer.apple.com/documentation/coregraphics/quartz_event_services
- NSWorkspace: https://developer.apple.com/documentation/appkit/nsworkspace
- Notarizing: https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution
- Launch agents: https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html
- `AXIsProcessTrustedWithOptions`: https://developer.apple.com/documentation/applicationservices/1459186-axisprocesstrustedwithoptions
