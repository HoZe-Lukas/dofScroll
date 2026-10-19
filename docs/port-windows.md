<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Windows port

Status (package W1, branch `feat/w1-windows`): the backend is implemented and compiled in CI
(MSVC and MinGW-w64 on `windows-latest`), its pure logic is unit-tested, but **nothing has been run
against a real SpaceMouse or a real desktop session**. Every behavior statement below is marked
*CI* (checked by the build or a unit test), or *unverified* (needs a human on Windows, see
[port-windows-checklist.md](port-windows-checklist.md)). Read [port-architecture.md](port-architecture.md)
first. API facts come from Microsoft documentation as known to the authors and were not re-checked
on a live system.

## What exists

| Part | Files | How it works | Checked by |
|---|---|---|---|
| Shared core | `src/*.cpp`, `src/platform/fs_win.cpp` | Same config parser, DSP, modes, profiles and daemon core as on Linux. File access goes through the file-system layer (reparse-point refusal, owner SID check, protected DACLs, atomic replace; see `security.md` section 9). | *CI*: all unit tests run on MSVC and MinGW |
| Raw-HID input | `src/platform/win/win_hid_input.*`, `src/platform/hid_input_core.hpp`, `src/platform/event_queue.hpp` | `hid.dll`/SetupAPI enumeration of HID devices, filtered by vendor/product id against `devices/*.conf`; one reader per physical device; reports are decoded by `src/hid_descriptor.*` and turned into events per device by `HidInputCore`. | *CI*: decoding, per-device state, event queue (pure code); the Win32 calls only *compile* in CI |
| Output | `src/platform/win/win_output.*`, `win_key_tables.hpp` | `SendInput`: `MOUSEEVENTF_WHEEL` / `HWHEEL` (one detent = `WHEEL_DELTA` 120), virtual-key codes for every `Key` in `src/keys.hpp`, mouse buttons 1-3. `releaseAll()` / `forceReleaseAll()` release everything held. | *CI*: key table completeness and mapping (pure code); `SendInput` itself is *unverified* |
| Active window | `src/platform/win/win_platform.cpp` | Foreground window -> process id -> executable file name, lower-cased (`firefox.exe`), used as the key for `[app:...]` profiles. Processes that deny access give an empty name (default profile). | *unverified* |
| Paths | `src/platform/path_rules.hpp`, `win_platform.cpp` | Config `%APPDATA%\dofscroll` (modes in `modes\` below it), runtime files (status, log) `%LOCALAPPDATA%\dofscroll`. Path rules are pure functions tested on every OS. | *CI*: path rules; directory creation with a private ACL is exercised by `test_fs` on the runner |
| Reload | `win_platform.cpp` | The writer replaces the config atomically, then signals a named event; the daemon polls it on its main thread (`ReloadListener::pollReload`). `requestReload()` is what a tray program calls. Idempotent. | *unverified* end to end |
| Daemon `main` | `src/platform/win/win_main.cpp`, `cmake/windows.cmake` | GUI-subsystem executable `dofscroll.exe` (no console window), wires the pieces above into `DaemonCore` like `src/dofscroll.cpp` does on Linux; log and status files under the runtime directory. | *CI*: builds; `dofscroll.exe --version` smoke test runs on the runner |
| Autostart | `packaging/windows/` | Per-user `HKCU\...\Run` value (or Startup shortcut), no administrator rights. | *CI*: real registry round trip on the runner. That Windows starts the daemon at logon is *unverified* |

Not part of W1: tray / settings UI (package W3), installer and code signing (W4).

## Input details

- Enumerate HID interfaces with SetupAPI (`GUID_DEVINTERFACE_HID`), read vendor/product id with
  `HidD_GetAttributes`, keep only ids listed in a `devices/*.conf` description. The vendor/product
  id is also parsed from the device path (`...hid#vid_256f&pid_c635#...`) as a cheap pre-filter.
- Each matched device is opened read-only with sharing allowed and read with overlapped
  `ReadFile`; byte 0 of every report is the report id, exactly the layout `docs/devices.md`
  describes. One `DeviceInfo` (own id, name, vendor/product, factory button count) per physical
  device.
- Hot-plug: unverified. The implementation re-enumerates periodically and/or on device
  notifications; removal stops the reader and releases buttons and motion of that device.
- Not used: the 3Dconnexion SDK (proprietary license, incompatible with GPL-3.0-or-later; see
  `windows-mac.md`).
- The report layout of each model comes from the shipped description files and is the commonly
  used 3Dconnexion layout; it has **not** been verified on hardware (see `devices.md`). Axis
  signs and order relative to what spacenavd delivers on Linux are unverified; a wrong sign is
  fixed with a negative `scale` in the description file.

## Output details and limits

- Input sent to an elevated (administrator) window is blocked by UIPI unless the daemon runs
  elevated too; the secure desktop (lock screen, UAC prompt) cannot be injected into. Expected
  behavior, unverified in practice.
- Ctrl + wheel zoom is application specific. Whether it behaves as on Linux in browsers, Office,
  CAD or image tools is unverified.
- A daemon that is ended abnormally (Task Manager, crash) cannot release keys it holds;
  `dofscroll.exe --release-keys` (same as on Linux) sends releases for the keys and mouse buttons
  1-3 (never a bare Alt or Win key-up, which would open the menu bar or the Start menu). A normal
  daemon start does not do a blanket release; it only releases what it pressed itself, so keys the
  user holds are not touched. On log-off or shutdown the daemon releases its keys synchronously
  (WM_ENDSESSION, bounded to 3 s). `stop-daemon.ps1` asks the daemon to quit through the event
  `Local\dofscroll-quit` first, ends it after a timeout, then runs `--release-keys`.
  Compile/link only in CI, unverified on hardware: the end-session handling and the event.

## Running programs (picker)

`src/platform/win/running_apps_win.cpp` lists programs for the "Add application" dialog with
`EnumWindows` (visible, titled, non-tool, non-cloaked, unowned windows); the match string is the
same lower-case executable name as the foreground provider. Processes that deny access are skipped.
Compile-checked and smoke-tested by `running_apps_selftest`; unverified on a real desktop.

## Active window caveats (unverified)

- Packaged / UWP apps may report `ApplicationFrameHost.exe` as the foreground process instead of the
  real app, so a profile for such an app may not match.
- Matching uses the executable file name only (no path); two programs with the same file name
  share a profile.

## Files, logs, status

`%LOCALAPPDATA%\dofscroll\dofscroll.log` (rolling, same limits and sanitizing of device strings as
on Linux) and `dofscroll-status` (replaced atomically; same `key=value` format as `common.hpp`).
Config and modes are the same files as on Linux (`dofscroll.conf`, `modes\*.conf`) and can be
copied between machines unchanged.

## Autostart

Manual setup from the build artifact, see `packaging/windows/README.md`:
`install-autostart.ps1 -ExePath <path to dofscroll.exe>`. A Windows service is not suitable
(input injection needs the interactive session). Behavior across fast user switching is
unverified.

## Vendor driver

If the 3Dconnexion driver is installed it may also translate motion into scroll/zoom/keys for some
applications, so results double up. Disable per-application navigation and button assignments in
the driver panel for the apps handled here, or exit the driver's background process while testing.
Whether the vendor driver prevents a second reader from opening the device (exclusive access) or
whether both receive every report is **unresolved**; the checklist tests both cases (driver running
and stopped). If opening fails, the daemon logs the Windows error and retries.

## Signing

Unsigned executables trigger SmartScreen warnings and are more likely to be flagged by antivirus.
Code signing belongs to package W4; CI artifacts are for testing only.

## References

- Raw input overview: https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input
- HID support routines: https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/
- `SendInput`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- `MOUSEINPUT` (wheel flags): https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput
- `GetForegroundWindow`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow
- `QueryFullProcessImageNameW`: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew
- SignTool: https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool
