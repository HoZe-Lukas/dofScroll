# Windows port

Status: design only, nothing implemented or tested on Windows. Read `port-architecture.md`
first. The API facts below come from Microsoft documentation as known to the author; the pages
were not re-fetched for this draft, so everything is "unverified" until checked on a real system.

## Input: raw HID, no vendor SDK

- Register with `RegisterRawInputDevices` for the generic-desktop usage page (multi-axis
  controller usage) with `RIDEV_INPUTSINK` so reports arrive without focus, and receive
  `WM_INPUT` in a hidden message-only window. Enumerate devices with
  `GetRawInputDeviceList`/`GetRawInputDeviceInfo` to get one handle per physical device (this is
  what makes per-device profiles possible). Unverified: exact usage page/usage values per
  SpaceMouse model, and whether `RIDEV_INPUTSINK` suffices while the vendor driver is running.
- Alternative if RawInput is not enough: open the HID device directly (hidapi or the HID APIs).
  Exclusive access by the vendor driver may block this. Unverified.
- Not used: the 3Dconnexion SDK (proprietary license, incompatible with GPL-3.0; see
  `windows-mac.md`).
- Common models carry translation and rotation as separate report IDs and buttons as a bitmask;
  the layout must be confirmed per device. Unverified.

## Output: `SendInput`

Mapping of the `Output` operations: vertical wheel = `MOUSEEVENTF_WHEEL`, horizontal wheel =
`MOUSEEVENTF_HWHEEL` (one notch = `WHEEL_DELTA`, 120), keys = `KEYEVENTF_*` with virtual-key
codes (Ctrl held for zoom, arrow keys tapped in nav mode), mouse buttons = `LEFTDOWN/UP`,
`MIDDLEDOWN/UP`, `RIGHTDOWN/UP`. `forceReleaseAll` sends key-up/button-up regardless of state.
Caveats: input to elevated (administrator) windows is blocked by UIPI unless the daemon is
elevated too, and the secure desktop (lock screen, UAC) cannot be injected into. Both are
expected behavior, unverified in practice.

## Active window to process name

`GetForegroundWindow` -> `GetWindowThreadProcessId` -> `OpenProcess` with limited-query rights ->
`QueryFullProcessImageName`; use the lower-cased executable name as the profile key. Poll on a
timer or use `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`. Processes that deny access yield an empty
name, which falls back to the default profile. Unverified for UWP/packaged apps (the host
process `ApplicationFrameHost.exe` may hide the real app).

## Daemon without console

Build as a GUI-subsystem executable (no console window) with a hidden message-only window for
raw input and the reload ping. Logging goes to a file under `%LOCALAPPDATA%`. The tray program is
a separate GUI process, as on Linux.

## Autostart

Per user: a value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` or a shortcut in the
Startup folder; the installer chooses. A Windows service is not suitable because input injection
needs the interactive user session. Unverified: behavior across fast user switching.

## Signing

Unsigned executables trigger SmartScreen warnings and are more likely to be flagged by
antivirus. Plan: code-sign daemon, tray and installer with a code-signing certificate
(`signtool`, with a timestamp). Which certificate type (OV/EV or a hosted signing service) is
open. Distribution without signing is for testing only.

## Vendor driver settings to disable

If the vendor driver is installed and also translates motion into scroll/zoom or key events for
some applications, results double up. In the driver panel, disable per-application navigation
and button assignments for the apps where this tool is active, or exit the driver's background
process while testing. Whether the vendor driver blocks raw HID access to the device is
unresolved; test with and without it. See `windows-mac.md` for what the driver can configure.

## References

- Raw input overview: https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input
- `SendInput`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- `MOUSEINPUT` (wheel flags): https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput
- `GetForegroundWindow`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow
- `QueryFullProcessImageNameW`: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew
- SignTool: https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool
