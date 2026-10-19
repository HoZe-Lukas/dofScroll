# Port architecture: platform layer

Goal: one shared core (config, DSP, modes, profile matching, key mapping) and a thin platform
layer per OS. This document describes the seams; `port-windows.md` and `port-macos.md` describe
the two new backends. Status: the interfaces exist in `src/platform/platform.hpp` (with a scripted
`FakeInputSource` for tests); the Linux implementation and the Windows/macOS backends are not
implemented yet (see `docs/work-plan.md`).

## Platform layer

| Seam | Linux today | Windows | macOS |
|---|---|---|---|
| `InputSource` (new): delivers 6-DoF motion and button events per device | libspnav via spacenavd | raw HID via RawInput | IOHIDManager |
| `Output` (exists, `src/output.hpp`) | X11/XTest or uinput | `SendInput` | CGEvent |
| `ActiveWindow` (exists, `src/activewin.hpp`) | X11 window class | foreground window to process name | NSWorkspace frontmost app |
| Paths and permissions | `XDG_RUNTIME_DIR`, `~/.config` | `%LOCALAPPDATA%` / `%APPDATA%` | `~/Library/Application Support` |
| Reload and status | SIGHUP, status file | named event or message window, status file | notification or socket, status file |

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

## Logging and status files

The status file (`key=value` lines, replaced atomically) and the log keep their format. Only the
location changes: a per-user runtime/cache directory with user-only permissions. Logging goes to
a file (Windows has no console; macOS has the unified log or a file under `~/Library/Logs`),
with the same sanitizing of device-supplied strings as on Linux.

## Build

CMake is the cross-platform build; the Makefile remains for Linux. Targets: core library
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

## Open points

- The HID report layout of each device generation (translation and rotation as separate reports,
  button bitmask) must be checked per product; unverified here.
- Interaction with the vendor driver that already claims the device (see the OS docs).
- Hot-plug handling and sleep/wake on both OSes.
