<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Prompt for the Windows / macOS port session

Paste the block below into a Claude Code session started in a clone of this repository **on the
machine you want to port to** (Windows or macOS). The same prompt works for both: the session
detects the OS and takes package W1 (Windows) or W2 (macOS) from `docs/work-plan.md`.

```text
You are porting dofscroll to the operating system you are running on.

1. Detect the OS (`uname -s`; on Windows `ver` or $env:OS). Windows -> package W1, branch
   feat/w1-windows. macOS (Darwin) -> package W2, branch feat/w2-macos. Linux -> stop and tell me.
2. Read, in this order: AGENTS.md, docs/work-plan.md, docs/port-architecture.md,
   docs/port-windows.md or docs/port-macos.md (yours), docs/windows-mac.md, docs/devices.md,
   src/platform/platform.hpp, src/daemon_core.hpp, src/platform/linux/ (the reference
   implementation of every seam), src/hid_descriptor.hpp, devices/*.conf.
3. Create your branch, set your package status in docs/work-plan.md to "in progress (<OS>, <date>)"
   in the first commit and push the branch. Install the toolchain you need (Windows: Visual Studio
   Build Tools or MSYS2/mingw, CMake; macOS: Xcode command line tools, CMake) and run
   `scripts/install-hooks.sh`.
4. Deliver, in NEW files only (src/platform/<win|mac>/, cmake/<windows|macos>.cmake,
   packaging/<windows|macos>/):
   - InputSource: raw HID (Windows: SetupAPI + hid.dll or RawInput; macOS: IOHIDManager). No vendor
     SDK, ever. Decode reports with src/hid_descriptor.* and devices/*.conf; one DeviceInfo per
     physical device (this is the multi-device advantage over spacenavd). Handle hot-plug and the
     vendor driver already holding the device (document what you find).
   - Output: Windows SendInput (wheel, hwheel, all Keys from src/keys.hpp, mouse buttons 1-3);
     macOS CGEventPost. Unit-test the key/button tables like src/key_tables.hpp.
   - ActiveWindowProvider: Windows foreground window -> process executable name; macOS
     NSWorkspace frontmost app (Objective-C++, bundle identifier or name).
   - platformPaths(): Windows %APPDATA%\dofscroll (config) and %LOCALAPPDATA%\dofscroll (runtime);
     macOS ~/Library/Application Support/dofscroll and ~/Library/Caches/dofscroll. User-only
     permissions.
   - ReloadListener + requestReload(): Windows named event; macOS distributed notification or a
     user-only Unix socket. Contract in platform.hpp.
   - A thin `main` that wires these into the daemon core exactly like src/dofscroll.cpp does for
     Linux; a console-less Windows build; autostart (Windows: Startup entry or scheduled task at
     logon; macOS: LaunchAgent plist); log and status files as in docs/port-architecture.md.
   - CMake: build the new backend only on its OS; the shared core and all existing tests must build
     and pass on your OS (`ctest`). Do not break the Linux build.
5. Rules: everything in the repo is English; SPDX header in new files; do not edit core files
   (src/dofscroll.cpp, src/daemon_core.*, src/config.*, src/modes.*) except for small, justified
   fixes that you list in your report; no secrets or personal data; do not use the 3Dconnexion SDK.
   Backends must be unit-testable without hardware (use FakeInputSource).
6. Verify: build, run ctest and `cmake --build build --target check-fast`. Then, with the device
   attached, check that the reports decode as devices/*.conf says (use tools/hidprobe if it exists
   on main, otherwise write a small probe test) and report the real layout. Change
   devices/*.conf only with values you measured. Run the daemon and report what works: motion,
   buttons, scrolling, zoom, per-app profile switching.
7. Finish: push your branch and open a pull request (do not merge). Set your work-plan row to its
   final state. In the PR list exactly what you tested on hardware, what only compiled, and what is
   still open. Never claim hardware behavior you did not observe.
```
