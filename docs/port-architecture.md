# Port architecture: platform layer

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver).

Goal: one shared core (config, DSP, modes, profile matching, key mapping) and a thin platform
layer per OS. This document describes the seams; `port-windows.md` and `port-macos.md` describe
the two new backends. Status: the interfaces exist in `src/platform/platform.hpp` (with a scripted
`FakeInputSource` for tests); the Linux implementation lives in `src/platform/linux/` (daemon core: `src/daemon_core.*`); the Windows backend is implemented on branch `feat/w1-windows` (compiled and unit-tested in CI,
not run on hardware; see `docs/port-windows.md`), the macOS backend is not merged yet (see
`docs/work-plan.md`).

## Platform layer

| Seam | Linux today | Windows | macOS |
|---|---|---|---|
| `InputSource` (new): delivers 6-DoF motion and button events per device | libspnav via spacenavd | raw HID (SetupAPI + `hid.dll`, `ReadFile`), shared `HidInputCore` | IOHIDManager |
| `Output` (exists, `src/output.hpp`) | X11/XTest or uinput | `SendInput` | CGEvent |
| `ActiveWindow` (exists, `src/activewin.hpp`) | X11 window class | foreground window to process name | NSWorkspace frontmost app |
| Paths and permissions | `XDG_RUNTIME_DIR`, `~/.config` | `%LOCALAPPDATA%` / `%APPDATA%` | `~/Library/Application Support` |
| Reload and status | SIGHUP, status file | named event, status file | notification or socket, status file |

`InputSource` is the only new abstraction: today the daemon talks to libspnav directly. It should
expose "device list changed", "motion (6 axes, per device id)" and "button (index, down/up)" and
hide whether the data comes from spacenavd or from HID reports. The core stays identical;
`Output` and `ActiveWindow` already have the right shape (the `Output` operations map one-to-one
onto the OS calls listed in the OS docs).

## Config files

The config format is identical on all OSes: same keys, same parser (`src/config.cpp`), same
example files in `examples/`. A config copied between machines must work unchanged. Only the
default location differs per OS (see table above). Profile names match the process name
(Windows: executable name, macOS: bundle identifier or app name); the matching rules live in the
shared profile matcher, only the string fed into it is platform specific.

## Reload mechanism replacing SIGHUP

Windows has no SIGHUP and macOS agents should not rely on signals for settings. Replace the
signal with a small "reload" channel owned by the daemon: the tray writes the config atomically
(as today), then pings the daemon; the daemon re-reads the file on its main thread. Candidates:
a named event (Windows), a distributed notification or Unix socket (macOS), or file watching as
a portable fallback. The choice is open (unverified which is most robust); the contract is
"config written atomically, then ping; reload is idempotent".

What the Qt UI does (`spn::requestReload`, client side in `src/platform/<os>/client_*.cpp`, linked by
the daemon and the UI): every save goes through `SettingsModel::save()`, which writes the file
atomically and then calls the reload callback of the `UiContext` (one `requestReload` per save).
Linux: `systemctl --user kill -s HUP dofscroll.service` (the daemon catches SIGHUP). Windows:
`SetEvent` on the named event `Local\dofscroll-reload` that the daemon created (fails with "not
running" when there is no daemon). macOS: connecting to the user-only socket
`<runtimeDir>/reload.sock` (the connection is the request; foreign or stale sockets are refused).
A failed ping never loses the save: the page shows the reason and the file stays written.

## Logging and status files

The status file (`key=value` lines, replaced atomically) and the log keep their format. Only the
location changes: a per-user runtime/cache directory with user-only permissions. Logging goes to
a file (Windows has no console; macOS has the unified log or a file under `~/Library/Logs`),
with the same sanitizing of device-supplied strings as on Linux.

## Build

CMake is the build system on all platforms. Targets: core library
(config, DSP, modes, seed, device DB), the daemon, the tray/settings UI, and the unit tests,
which run on every platform because the core has no OS dependencies. Platform backends are
selected by CMake and compiled only on their OS. The tray UI toolkit for the ports is not decided.

## Multi-device limitation and direct HID

spacenavd exposes all SpaceMice as one merged stream, so libspnav cannot tell which physical
device produced an event; per-device profiles and per-device button counts are therefore
impossible on Linux through that path. With direct HID access each device is an individual
handle with its own vendor/product ID, name and report stream: the daemon can keep one state
(DSP, mode, buttons) per device and look up its factory button count by name. This is the main
functional advantage of the ports, not only a licensing workaround.

The vendor SDK is not used on any OS: its license is proprietary and revocable, which is
incompatible with distributing a GPL-3.0-or-later program (see `windows-mac.md`). This is a
project decision, not legal advice.

## Running programs picker

`src/platform/running_apps.hpp` defines `RunningAppsProvider` (list of running programs for the
"Add application" dialog). Its `matchString` must be byte-identical to what the
`ActiveWindowProvider` of the same OS returns for that program. Pure logic (filter, sort,
capture-next-focus, dedupe) lives in `src/ui/model/app_picker_model.hpp` and
`src/platform/running_apps_util.hpp`.

- Linux/X11: done (`platform/linux/running_apps_x11.cpp`); Wayland returns an empty list with an error.
- Windows: done, compile-checked (`platform/win/running_apps_win.cpp`): `EnumWindows` over visible,
  non-tool, non-cloaked, titled, unowned top-level windows, `QueryFullProcessImageNameW` (access
  denied: window skipped), match string through `winMatchString` (the foreground provider's
  `exeNameLower` rule; the UWP frame host is never listed), display name from the version resource.
- macOS: done, compile-checked (`platform/mac/running_apps_mac.mm`): `NSWorkspace.runningApplications`
  with `activationPolicy == regular`, match string through `macMatchString` (bundle identifier,
  else localized name, lowercased, as `mac_activewin.mm`), icon hint = bundle path.
- The pure rules are in `platform/running_apps_os.hpp` (tested on Linux in
  `tests/test_running_apps_os.cpp`). `running_apps_selftest` runs the provider on the Windows/macOS
  runner and asserts only "no crash, well-formed list" (an empty list is accepted).
- Device list for `DevicePickerModel`: `listInputDevices()` opens the platform `InputSource`, takes
  its `devices()` snapshot and closes it again.

## Live feed

The UI needs the six axis values, the button state and the daemon's interpretation at about 30 Hz
(axes view, axis assistant). On Linux the GTK tray can read libspnav itself; on Windows and macOS the
daemon owns the device, so it publishes a feed (`LIVE_FEED = 1`, default). Code in `src/live_feed/`,
security notes in `security.md` section 10.

- Protocol v1 (`live_feed_protocol.hpp`): one-way daemon -> client, binary little-endian frames with
  a header (`"DFLV"`, version, type, payload length) and a fixed sample payload (seq, 6 axes, button
  mask, channel outputs v/h/zoom after DSP, flags, mode id). Frames are at most 86 bytes; version
  changes are incompatible and detected by the client. No command channel.
- Transport behind one interface (`live_feed.hpp`: `FeedListener`, `FeedConn`): Unix domain socket
  `live.sock` in the runtime directory (Linux, macOS), named pipe `\\.\pipe\dofscroll-live-<sid>`
  (Windows).
- `LiveFeedServer` is fed by `DaemonDeps::liveFeed` once per tick, rate limits to 30 Hz, only works
  while a client is connected and never blocks. `LiveFeedClient` (toolkit-free, reconnecting) and
  `src/ui/model/live_feed_model.hpp` (latest sample, min/max/peak tracking) serve the UI.

## Live feed

The UI needs the six axis values, the button state and the daemon's interpretation at about 30 Hz
(axes view, axis assistant). On Linux the GTK tray can read libspnav itself; on Windows and macOS the
daemon owns the device, so it publishes a feed (`LIVE_FEED = 1`, default). Code in `src/live_feed/`,
security notes in `security.md` section 10.

- Protocol v1 (`live_feed_protocol.hpp`): one-way daemon -> client, binary little-endian frames with
  a header (`"DFLV"`, version, type, payload length) and a fixed sample payload (seq, 6 axes, button
  mask, channel outputs v/h/zoom after DSP, flags, mode id). Frames are at most 86 bytes; version
  changes are incompatible and detected by the client. No command channel.
- Transport behind one interface (`live_feed.hpp`: `FeedListener`, `FeedConn`): Unix domain socket
  `live.sock` in the runtime directory (Linux, macOS), named pipe `\\.\pipe\dofscroll-live-<sid>`
  (Windows).
- `LiveFeedServer` is fed by `DaemonDeps::liveFeed` once per tick, rate limits to 30 Hz, only works
  while a client is connected and never blocks. `LiveFeedClient` (toolkit-free, reconnecting) and
  `src/ui/model/live_feed_model.hpp` (latest sample, min/max/peak tracking) serve the UI.

## Open points

- The HID report layout of each device generation (translation and rotation as separate reports,
  button bitmask) must be checked per product; unverified here.
- Interaction with the vendor driver that already claims the device (see the OS docs).
- Hot-plug handling and sleep/wake on both OSes.
