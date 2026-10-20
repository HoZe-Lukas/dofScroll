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
(axes view, axis assistant). On Linux the interface can read libspnav itself; on Windows and macOS the
daemon owns the device, so it publishes a feed (`LIVE_FEED = 1`, default). Code in `src/live_feed/`,
security notes in `security.md` section 10.

- Protocol v2 (`live_feed_protocol.hpp`): one-way daemon -> client, binary little-endian frames with
  a header (`"DFLV"`, version, type, payload length) and a fixed sample payload (seq, 6 axes, button
  mask, channel outputs v/h/zoom after DSP, flags, mode id). Version changes are incompatible: a
  client that sees another version stops with the state `Incompatible` and the error "protocol
  version mismatch (...)" (v1 peers have no device info frame). No command channel.
- Device info frame (type 2), sent right after connect and on every device-list change: at most 8
  devices, each with id, USB vendor:product (0 = unknown), reported button count, the factory
  button count (`factoryButtonCount(name, reported)` from `device_db.hpp`: the number the UI
  uses for configurable buttons), axis count and a UTF-8 name of at most 64 bytes. The encoder
  sanitizes (invalid bytes and control characters become `?`, truncation at a code point
  boundary); the client treats all of it as untrusted and ends the stream on invalid UTF-8, control
  characters, counts out of range, inconsistent lengths or more than 8 devices. Frames are at most
  601 bytes. The UI model (`LiveFeedModel::devices()`) exposes the list; without a feed the Profiles
  page falls back to the `device=` line of the status file.
- Transport behind one interface (`live_feed.hpp`: `FeedListener`, `FeedConn`): Unix domain socket
  `live.sock` in the runtime directory (Linux, macOS), named pipe `\\.\pipe\dofscroll-live-<sid>`
  (Windows).
- `LiveFeedServer` is fed by `DaemonDeps::liveFeed` once per tick, rate limits to 30 Hz, only works
  while a client is connected and never blocks. `LiveFeedClient` (toolkit-free, reconnecting) and
  `src/ui/model/live_feed_model.hpp` (latest sample, min/max/peak tracking, device list) serve the UI.

## If the driver holds the device exclusively

Status: **a plan, nothing of it is implemented, and whether it is needed is not known.** dofscroll is
meant to run next to the official 3Dconnexion driver (3DxWare, 3Dconnexion Software), which the user
keeps installed and running for CAD programs; "quit the driver" is not an acceptable answer. The raw
HID readers (`win_hid_input.cpp`: shared open, never exclusive; `mac_hid_input.cpp`: IOHIDManager
without seize) may or may not receive reports while the driver runs. The guided test
(`scripts/test-driver-coexistence.*`, issues #36 and #37) decides. If it shows that Windows or macOS
blocks the second reader, this is the implementation plan, in this order.

Facts below come from public, partly old sources (forum posts from about 2007 to 2015, third-party
code). Each is marked **[source]** with a link or **[unverified]**; the current vendor SDK
documentation was not read (it needs the SDK download and its license).

### (i) Read the SpaceMouse through the vendor's client API, loaded at runtime

- **macOS: `3DconnexionClient.framework`.** An application registers with the driver
  (`RegisterConnexionClient(signature, name, mode, mask)`), installs a message handler and receives
  axis data (`kConnexionCmdHandleAxis`) and button data (`kConnexionCmdHandleButtons`); a 3Dconnexion
  answer for OS X developers names mode `kConnexionClientModeTakeOver` with mask `kConnexionMaskAxis`
  (or `kConnexionMaskAll`), and says the handler must compare the client id in each message with its
  own. [source: <https://forum.3dconnexion.com/viewtopic.php?p=714>,
  <https://forum.3dconnexion.com/viewtopic.php?p=732>; FreeCAD uses TakeOver with `kConnexionMaskAll`:
  <https://gitea.suzanne.soy/suzanne.soy/FreeCAD/commit/b5688aade5daaccbbaf1ed7e067ff858f9f4ab17>]
- **Does the driver deliver events to a client that is not frontmost?** One 2007 report: a client with
  its own signature got events only while its application was in front, a wildcard client
  (`kConnexionClientWildcard`) got them regardless of focus; the same answer advises against the
  wildcard because per-application settings in the preference pane need a real signature.
  [source: <https://forum.3dconnexion.com/viewtopic.php?p=732>] For a background program such as
  dofscroll this is the central question. **[unverified for current drivers]**
- **What take-over mode does to the CAD program** (does the driver stop sending to the CAD program
  while a take-over client exists, or only to the frontmost one?) is not documented in the sources
  found. **[unverified]** It is the risk of this option: dofscroll must not take the device away from
  the CAD program. Registering with a mask of the axes only, and testing with a CAD program running,
  is the first experiment.
- **What registering needs:** the framework (present on a machine that has the driver), a signature
  and a name; no window. **[source, 2007]**
- **Windows: 3DxWare input API (`TDxInput`, COM).** A 3Dconnexion staff member said the COM API can
  deliver events without a window handle and that a hidden window also works for a background program;
  events can be polled or received through a connection point. [source:
  <https://forum.3dconnexion.com/viewtopic.php?p=21138>, <https://forum.3dconnexion.com/viewtopic.php?p=3833>]
  A moderator called `TDxInput` unsupported, and developers point to the Raw Input API or the newer
  3DxWare 10 SDK instead [source: <https://forum.3dconnexion.com/viewtopic.php?p=15566>]. Whether keys
  and buttons that the driver maps to its own functions still arrive is unclear; one post says
  they must be assigned as plain buttons in the driver panel [same source]. **[unverified for current
  drivers; the current SDK name and interface were not looked up]**
- **License (needs a decision by the maintainer).** The SDK is proprietary, see
  [windows-mac.md](windows-mac.md#sdk-access-and-license): its license restricts redistribution and use
  of the headers and libraries. Loading the framework or the COM server that the user's own driver
  installed, at runtime (`dlopen` on the framework path, `CoCreateInstance` on the registered class),
  without bundling any SDK file and without the SDK headers (the few declarations written from the
  public documentation, if that is allowed), keeps the SDK out of the dofscroll binary. Whether that is
  enough for a GPL-3.0-or-later program, and whether the SDK license allows writing such declarations at
  all, is a legal question that nobody here has answered. Do not implement until the maintainer decides.

### (ii) A small broker (as Space Juggler does)

A tool called Space Juggler was named to the maintainer as a macOS broker between the driver and other
programs; its source and README were not read for this plan and a web search did not find the project,
so what follows is the general idea, not a description of that tool **[unverified]**. The problem
it addresses is reported elsewhere: from macOS Big Sur on the driver opens the device exclusively, so
HIDAPI readers get nothing [source: <https://godotforums.org/d/28882/51>], and a plugin author says
the proper fix is the client framework API, not direct reads [source: <https://pypi.org/project/napari-spacemouse>].
The idea: one small helper process is the only client of the
driver (through the client API of option (i), or by reading the device when the driver is not there),
and forwards the data to dofscroll, so that dofscroll never has to compete for the device. It moves
the license and take-over questions of (i) into the helper and adds a process to install, start and
keep alive; it is only worth it if (i) works but cannot be done inside the daemon. **[unverified]**

### (iii) No access for dofscroll where the driver holds the device

Tell the user to use the driver's own per-application profiles (documented in
[windows-mac.md](windows-mac.md)) for what the driver already does, and keep dofscroll for what the
driver does not do (shaped scroll and zoom from the six axes, keys from axes, per-program and
per-device profiles). This is the fallback if (i) and (ii) turn out to be impossible or not
permitted; it loses most of dofscroll on that system, so it is the last option, not the plan.

### What exists now

When the open is refused, the daemon logs an explicit line and the interface shows "SpaceMouse found
but cannot be read" (`src/platform/input_diag.hpp`, status key `input_issue`); a device that opens
but sends nothing for 45 seconds gets a softer hint. Both are covered by unit tests; neither has met a
real driver.

## Open points

- The HID report layout of each device generation (translation and rotation as separate reports,
  button bitmask) must be checked per product; unverified here.
- Interaction with the vendor driver that already claims the device (see the OS docs).
- Hot-plug handling and sleep/wake on both OSes.
