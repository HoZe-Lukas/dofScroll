# macOS port

Status: design only, nothing implemented or tested on macOS. Read `port-architecture.md` first.
The API facts below come from Apple documentation as known to the author; the pages were not
re-fetched for this draft, so everything is "unverified" until checked on a real system.

## Input: IOHID, no vendor SDK

- Use `IOHIDManager` with a matching dictionary for the device usage (multi-axis controller;
  exact page/usage per model unverified) and register input-value or input-report callbacks on
  a run loop. Each matched `IOHIDDevice` is one physical device, which gives the per-device
  state described in `port-architecture.md`. Hot-plug comes from the matching/removal callbacks.
- Not used: the `3DconnexionClient` framework (part of the proprietary SDK; license
  incompatible with GPL-3.0, see `windows-mac.md`).
- The vendor driver may seize the device exclusively; opening it from another process can then
  fail. Unverified: whether non-exclusive reading works while the vendor driver runs.
- Input Monitoring permission is required for reading HID input (see Permissions).

## Output: CGEvent

Mapping of the `Output` operations: scroll = `CGEventCreateScrollWheelEvent` (line or pixel
units; vertical and horizontal as two wheel axes), keys = `CGEventCreateKeyboardEvent` with
virtual key codes (Control held via event flags for zoom, arrows tapped in nav mode), mouse
buttons = `CGEventCreateMouseEvent` (left/right/other, button number 2 for middle), all posted
with `CGEventPost`. `forceReleaseAll` posts key-up/mouse-up regardless of state. Unverified:
whether Ctrl+scroll zoom behaves as on Linux in each target app (macOS apps often use Cmd or
pinch).

## Active app: NSWorkspace

`NSWorkspace.sharedWorkspace.frontmostApplication` gives the bundle identifier and localized
name; use the bundle identifier as the profile key and observe
`NSWorkspaceDidActivateApplicationNotification` instead of polling. This needs a small
Objective-C++ translation unit (`.mm`) that exposes the plain C++ `ActiveWindow` interface;
the rest of the code stays C++17.

## Permissions

- **Accessibility** is required to post synthetic events with `CGEventPost`
  (`AXIsProcessTrustedWithOptions` checks/prompts).
- **Input Monitoring** is required to receive HID input (`IOHIDCheckAccess` /
  `IOHIDRequestAccess`).
- Both are granted per binary in System Settings > Privacy & Security; the grant is tied to
  the code signature, so changing the signature or path can invalidate it. The daemon should
  detect missing permission, write it to the status file and show it in the tray instead of
  failing silently. Unverified: exact prompts per macOS version.

## LaunchAgent

Run daemon and tray as per-user LaunchAgents (`~/Library/LaunchAgents/*.plist`, with
`RunAtLoad` and `KeepAlive`). A LaunchDaemon (system level) is unsuitable: it has no GUI session
and cannot inject events into the user's session. Logs go to `~/Library/Logs` or the unified log.

## Signing and notarization

Distribute as a signed app bundle (Developer ID Application certificate, hardened runtime),
notarized with `notarytool` and stapled; otherwise Gatekeeper blocks first launch. A stable
signature also keeps the privacy grants stable across updates. This needs a paid Apple
developer account; unsigned local builds work for testing only.

## Vendor driver settings

The vendor driver (System Settings pane) can translate motion or buttons into events for
applications; turn off per-app navigation and button assignment for apps handled by this
tool, or quit the driver while testing, to avoid doubled input. See `windows-mac.md` for what
the driver exposes and the reported lack of generic scrolling in browsers.

## References

- IOHIDManager: https://developer.apple.com/documentation/iokit/iohidmanager_h
- Quartz Event Services (CGEvent): https://developer.apple.com/documentation/coregraphics/quartz_event_services
- NSWorkspace: https://developer.apple.com/documentation/appkit/nsworkspace
- Notarizing: https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution
- Launch agents: https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html
- `AXIsProcessTrustedWithOptions`: https://developer.apple.com/documentation/applicationservices/1459186-axisprocesstrustedwithoptions
