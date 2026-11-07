<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Statements about the vendor driver: check against official documentation

Date of the check: 2026-11-06. Linked from [windows-mac.md](windows-mac.md).

Question: does every statement in this repository about the official 3Dconnexion driver (3DxWare on
Windows, 3Dconnexion Software on macOS), its SDK, `spacenavd` and `libspnav` agree with the official
documentation? Method and limits first, then the table.

## What could and could not be reached

| Source | Reachable? | How it was used |
|---|---|---|
| `spacenav.sourceforge.net` (home, FAQ, `man_libspnav`, `TDxInput.html`) | yes | read |
| `raw.githubusercontent.com` (FreeSpacenav `spacenavd`, `libspnav`; Blender; hidapi) | yes | source files read in full |
| `3dconnexion.com`, `forum.3dconnexion.com` | **no**: the host does not resolve from the check environment (outside its allowed network domains) | only through search-result summaries (WebSearch); marked "search summary" below, never as a first-hand text |
| `usb.org` (HID Usage Tables) | **no**, same reason | not checked |
| `learn.microsoft.com` | **no**, same reason | not checked |
| `docs.kernel.org`, `developer.apple.com` (the one page tried returned 404), `web.archive.org` | no / not usable | not checked |

The 3Dconnexion SDK documentation (the "Windows COM SDK Documentation" and the macOS client SDK) is
behind a developer registration and was **not read**. Everything below that depends on it is
"could not check". Where the only available evidence is the source code of another open-source
program that talks to the same driver (Blender), that is said in the table; it is a third-party
source, not official documentation, and it is labelled so.

Verdicts: **confirmed** (an official or primary source says it), **confirmed (code)** (the
upstream source code of the named project says it; for `spacenavd`/`libspnav` this is the project's
own implementation), **third-party only** (no official text; consistent with other programs'
code), **observed only** (found on a real machine, not documented), **contradicted**, **not
documented officially**, **could not check**.

## Table

### spacenavd and libspnav

| # | Claim (file:line) | Source and exact fact | Verdict |
|---|---|---|---|
| 1 | `spnav_open`, `spnav_poll_event`, `spnav_fd`, motion event fields `x y z rx ry rz`, button event `bnum`/`press` (`src/platform/linux/spnav_input.cpp:26-96`) | `libspnav` `src/spnav.h` (github.com/FreeSpacenav/libspnav): `int spnav_open(void)` ("Returns -1 on failure"), `int spnav_fd(void)`, `int spnav_poll_event(spnav_event *event)` ("returns the event type if available, or 0 otherwise"); `struct spnav_event_motion { int type; int x, y, z; int rx, ry, rz; unsigned int period; int *data; }`; `struct spnav_event_button { int type; int press; int bnum; }` | confirmed |
| 2 | Button index is 0-based (`spnav_input.cpp:94`, `docs/modes.md:232`, `docs/configuration.md:249`) | `man_libspnav` (spacenav.sourceforge.net/man_libspnav/): "0-based button number in sev.button.bnum"; `example-spnavrc`: "Button remapping (zero-based)" | confirmed |
| 3 | Axis meaning "libspnav convention": push forward/back is `z`, sideways `x`, lift/press `y`, tilt forward/back `rx`, tilt sideways `rz`, twist `ry` (`docs/configuration.md:183`, `docs/modes.md:553`) | `spacenavd` `src/dev.c` `read_device()`: for every USB device with `DF_SWAPYZ`, index swap table `{0, 2, 1, 3, 5, 4}`; with `DF_INVYZ` the value is negated unless index is 0 or 3. The 3Dconnexion USB entries in `usbdb[]` all carry `DF_SWAPYZ \| DF_INVYZ`. So from the raw HID values: libspnav y = -raw Z, z = -raw Y, rx = raw Rx, ry = -raw Rz, rz = -raw Ry (libspnav is the OpenGL-style frame, y up and z toward the viewer). `man_libspnav` and `spnav.h` give **no** direction or unit for the fields. | confirmed (code); direction and unit **not documented officially** |
| 4 | `spnavrc` options: `sensitivity`, dead zone, inversion, swap (`docs/configuration.md:552`, `docs/modes.md:229-241`) | `spacenavd` `doc/example-spnavrc`: `sensitivity`, `sensitivity-translation`/`-rotation` (+ per axis), `dead-zone` ("any motion less than this number is discarded as noise", example value 2), `dead-zoneN` per device axis, `invert-rot`/`invert-trans` ("combinations of the letters x, y, and z"), `swap-yz`, `axismapN`, `bnmapN`, `bnholdN`, `bnactN`, `kbmapN`, `led`, `grab`, `repeat-interval` | confirmed |
| 5 | `kbmapN = <keysym>` is experimental; `bnmapN`, `bnactN` are not key/click mappings; no click mapping exists (`docs/modes.md:235-241`) | `example-spnavrc`: "Map buttons to keyboard keys (experimental)"; `bnactN` actions are exactly `none, sensitivity-up, sensitivity-down, sensitivity-reset, disable-rotation, disable-translation, dominant-axis`; there is no click/mouse option in the file | confirmed |
| 6 | Config file is `/etc/spnavrc`; restart or SIGHUP re-reads it (`docs/modes.md:229`, `docs/configuration.md:555`) | `example-spnavrc` line 2: "copy it to /etc/spnavrc"; `spacenavd` `src/spnavd.c`: `signal(SIGHUP, sig_handler)`, handler writes to the self-pipe "to trigger a re-read" | confirmed (code) |
| 7 | Socket `/run/spnav.sock`, mode `0777` (`docs/security.md:38, 345`) | `libspnav` `src/spnav.c`: `#define SPNAV_SOCK_PATH "/var/run/spnav.sock"`; before that it tries `$SPNAV_SOCKET`, a `socket =` line in `/etc/spnavrc`, and `$XDG_RUNTIME_DIR/spnav.sock`. `spacenavd` `src/proto_unix.c`: `umask(0)` around `bind()` (world access). `/var/run` is normally a link to `/run`, so the documented path is the same file on current distributions, but the default is spelled `/var/run/spnav.sock`, and the client may use other paths. | confirmed (code), wording imprecise: see fix below |
| 8 | libspnav 1.x protocol lets a client change device settings (`docs/security.md:346`) | `spnav.h`: after "Everything from this point on ..." `spnav_client_name`, `spnav_evmask`, and configuration setters exist; "spnav_protocol() returns the protocol version understood by the running spacenavd" | confirmed |
| 9 | USB ids of the six shipped models come from the spacenavd table (`devices/*.conf`, `docs/devices.md`): 046d:c626 SpaceNavigator, 046d:c62b SpaceMouse Pro, 256f:c62e/c62f/c63a Wireless, 256f:c631/c632/c638 Pro Wireless, 256f:c633 Enterprise, 256f:c635 Compact | `spacenavd` `src/dev.c` `usbdb[]` has exactly these pairs with these model names. 3Dconnexion vendor ids are 0x046d and 0x256f. | confirmed (code) |
| 10 | Factory button counts 15 (Pro, Pro Wireless) and 31 (Enterprise) (`src/device_db.cpp`, `docs/devices.md`) | `spacenavd` `dev.c`: `bnhack_smpro` returns 15 for `bn < 0` ("button count"), `bnhack_sment` returns 31. Counts 2 for Compact/Wireless/SpaceNavigator: no spacenavd value; the product pages of 3Dconnexion were not reachable. | 15 and 31 confirmed (code); 2 could not check |
| 11 | "Several processes can read the same hidraw node" (`docs/devices.md:148`) | `spacenavd` reads **evdev** (`/dev/input/event*`, `EVIOCGRAB` when `grab` is true, `dev_usb_linux.c`), not hidraw; the grab affects other evdev users only. The kernel hidraw documentation was not reachable. | could not check (kernel doc); the spacenavd side is confirmed (code) |
| 12 | spacenavd reports one merged device when two SpaceMice are attached (`docs/configuration.md:530`, `docs/wiki/FAQ.md:30`, `readme.md:88`) | `spnav.h`: "TODO multi-device support and device selection not implemented yet" (`spnav_num_devices`, `spnav_select_device` are commented out); `spnavd.c` loops over all devices and feeds one client stream. The spacenav FAQ (entry 13) says "No. At the moment it's not possible to handle multiple devices and send events to client programs..." (older text). | consistent with the sources; the FAQ text is older and stricter than the code |
| 13 | Linux hidprobe: "3Dconnexion uses `046d` and `256f`" (`docs/devices.md:150`) | see row 9 | confirmed (code) |
| 14 | spacenavd "takes the device" so other readers (X server) should not use it (`docs/configuration.md:552`, FAQ) | `example-spnavrc`, "Device grab (USB)": "When set to true (default): grab the USB device to prevent other programs (such as the X server) from also using it." | confirmed |

### Windows: HID, raw input and the driver

| # | Claim (file:line) | Source and exact fact | Verdict |
|---|---|---|---|
| 15 | The device is a Generic Desktop multi-axis controller, usage page 0x01 usage 0x08 (`src/platform/win/win_hid_path.hpp:102`, `docs/port-windows-input-notes.md`) | Blender `GHOST_SystemWin32.cc` registers `usUsagePage = 0x01; usUsage = 0x08` ("multi-axis mouse (SpaceNavigator, etc.)"). The HID Usage Tables (usb.org) were not reachable. | third-party only; usb.org could not check |
| 16 | Report 1 = translation, report 2 = rotation, report 3 = button bitmask; three signed 16-bit little-endian values per axis report (`devices/*.conf`, `docs/devices.md`) | Blender `GHOST_SystemWin32.cc` `processNDOF`: `case 0x1` translation `short axis[3]` after the id byte, `case 0x2` rotation, `case 0x3` "Buttons bitmask (older devices)", and a combined report when `dwSizeHid == 13` (Blender: "I've never gotten one of these") | third-party only (consistent) |
| 17 | Full deflection is about +-350 (`src/platform/hid_input_core.hpp:9`, `driver_input_core.hpp:6`, `docs/devices.md`) | Blender `GHOST_NDOFManager.cc`: `scale = 1.0f / 350.0f; /* 3Dconnexion devices send +/- 350 usually */`. `spacenavd` `dev_usb_linux.c`: relative axes (`EV_REL`) are passed unchanged, absolute axes (`EV_ABS`) are rescaled to -500..500, so the libspnav value is about +-350 only for devices the kernel exposes as relative. | third-party only ("usually"); the claim "the libspnav scale" holds only for relative devices |
| 18 | The Pro, Pro Wireless and Enterprise `buttons = report=3 ... bits=32` lines are placeholders because their raw bits are not sequential (`devices/spacemouse-pro*.conf`, `docs/devices.md`) | `spacenavd` `bnhack_smpro`/`bnhack_sment` remap codes 256, 257, ... to button numbers. Blender `processNDOF` also handles `case 0x1c` "Buttons numbers (newer devices)" (an array of 16-bit button numbers) and `case 0x1d` (long press). dofscroll's HID path reads neither report id. | placeholder statement confirmed; **the lines are likely not enough for these models**: issue opened |
| 19 | Devices opened `GENERIC_READ`, `FILE_SHARE_READ\|FILE_SHARE_WRITE`, never exclusive (`win_hid_input.cpp:17,152`) | Microsoft `CreateFile` documentation not reachable. This is a statement about our own code, not about the driver. | code statement; Microsoft docs could not check |
| 20 | Sharing violation is Windows error 32, access denied is 5 (`docs/wiki/Requirements.md:116`) | `winerror.h` values (`ERROR_SHARING_VIOLATION` 32, `ERROR_ACCESS_DENIED` 5); Microsoft pages not reachable | could not check online |
| 21 | Whether the 3DxWare driver lets a second reader read the same HID device (`docs/port-windows-input-notes.md`, `docs/wiki/Requirements.md:110`) | No official statement was found. 3Dconnexion states nothing reachable about it. Third-party: Blender reads the same device through Raw Input (usage 0x08) and is documented to work next to the driver. | not documented officially (stays UNRESOLVED, correctly labelled) |
| 22 | 3Dconnexion recommends the Raw Input API over `TDxInput` (`docs/port-architecture.md:182` "A moderator called `TDxInput` unsupported, and developers point to the Raw Input API") | Search summary of forum.3dconnexion.com: a moderator wrote "We are now recommending that developers use the Microsoft Raw Input API instead of the TDxInput COM API." (thread not identified; the summary also says another moderator describes the Win32 SDK `Si*` functions as having "all the same capabilities as the 3DxInput COM server" and kept updated, and that Raw Input needs the application to interpret the values) | confirmed (forum moderator, via search summary, date unknown) |
| 23 | `TDxInput.Device` is a COM class registered by the driver (`src/platform/driver_paths.hpp:21`, `win_driver_api.hpp`) | Search summary of the 3Dconnexion forum: "The COM server is implemented as the dynamic load library TDxInput.dll which is automatically registered when 3DxWare is installed"; the moderator adds that it ships with no headers and points to the "Windows COM SDK Documentation" on the SDK download page | confirmed (forum, via search summary); the SDK documentation itself was **not read** |
| 24 | `Device`: `Sensor`, `Keyboard`, `IsConnected`, `Connect`, `Disconnect`; `Sensor`: `Translation`, `Rotation`; `Keyboard`: `Keys`, `IsKeyDown` (`win_driver_api.hpp:9-18`) | spacenav.sourceforge.net/TDxInput.html lists Device (Properties Sensor, Keyboard, Type, IsConnected; Methods Connect, Disconnect, LoadPreferences), Sensor (Translation, Rotation, Period), Keyboard (Keys, ProgrammableKeys, IsKeyDown, IsKeyUp, GetKeyLabel, GetKeyName), AngleAxis (X, Y, Z, Angle: "arbitrary units but euclidean norm"). **That page is not vendor text**: it documents the AerionInput reimplementation and says of one feature "The documentation says nothing about that" (the repo already calls it a reconstruction). | confirmed as a reconstruction; vendor text could not check |
| 25 | Units and range of `Translation` and `Angle`, key base 0 or 1, meaning of `Type` are unknown (`win_driver_api.hpp`, `driver_input_core.hpp:18-44`) | The reconstruction gives no units ("no unit is stated"). The vendor's COM SDK documentation may state them; it needs the developer download. | stays "not established"; maintainer action: read the SDK documentation |
| 26 | Axis order and signs of the Windows default (`y + rx`, `x - ry`, `-rz`) and the statement that the official driver names the axes differently (`docs/configuration.md:188`, `examples/default/windows/dofscroll.conf`) | The order is confirmed for the **raw** report (Blender `processNDOF` maps report values `axis[0..2]` to x, y, z as `{a0, -a2, a1}`, i.e. a0 = x, a1 = push, a2 = lift, and rotation likewise). The **signs** are not: the same Blender code, the spacenavd table (row 3) and the Linux default disagree with the Windows/macOS default in the relative sign of the translation and rotation terms. See "Findings that need a decision". | order confirmed (third-party code); signs **contradicted, needs hardware**: issue opened |

### macOS: client framework and driver

| # | Claim (file:line) | Source and exact fact | Verdict |
|---|---|---|---|
| 27 | Framework path `/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient` (`src/platform/driver_paths.hpp:16`) | Blender `GHOST_NDOFManagerCocoa.mm` `dlopen("/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient", ...)` and `bundleWithPath:@"/Library/Frameworks/3DconnexionClient.framework"`. Search summary: Blender and FreeCAD build files reference `/Library/Frameworks/3DconnexionClient.framework/Versions/Current/Headers/ConnexionClientAPI.h`. | third-party only (consistent, long-standing) |
| 28 | Constants and the `ConnexionDeviceState` layout (2-byte packing, 48 bytes): `kConnexionClientModeTakeOver` 1, `kConnexionMaskAll` 0x3fff, `kConnexionMaskAxis` 0x3f00, `kConnexionMaskAllButtons` 0xffffffff, `kConnexionCmdHandleButtons` 2, `kConnexionCmdHandleAxis` 3, `kConnexionMsgDeviceState` '3dSR', `kConnexionCtlGetDeviceID` '3did', `axis[6]` = TX TY TZ RX RY RZ (`src/platform/mac/mac_client_api.hpp:28-44`) | Blender `GHOST_NDOFManagerCocoa.mm` defines exactly these values and the comment `int16_t axis[6]; /* TX, TY, TZ, RX, RY, RZ. */`; `#pragma pack(push, 2)` | third-party only (identical); the vendor header was not read |
| 29 | Function signatures and the Pascal-string client name (`mac_client_api.hpp:55-65`) | Blender: `typedef int16_t (*SetConnexionHandlers_ptr)(MessageHandler, AddedHandler, RemovedHandler, bool);`, `RegisterConnexionClient('blnd', "\007blender", kConnexionClientModeTakeOver, mask)` | third-party only (identical) |
| 30 | "New drivers (3DxWare 10.8.4 and later, framework version 1.3.4.473) deliver buttons as application events when buttons are not requested" (`docs/port-architecture.md:310`) | Blender: "3DxMacCore version >= minimal_version is considered "new". It was firstly introduced in 3DxWare v10.8.4 r3716 and can process (not yet documented in SDK manual) kConnexionCmdAppEvent events", `new_driver_minimal_version = @"1.3.4.473"`; with a new driver Blender registers with `kConnexionMaskAxis` and button mask 0. | third-party only; the vendor says in no reachable place how raw buttons behave: Blender calls the app events "not yet documented in SDK manual" |
| 31 | dofscroll requests raw buttons (`kMaskAllButtons`) and ignores app events (`port-architecture.md:312`) | contradicts what Blender does for new drivers (it masks buttons away and uses app events), which `port-architecture.md` already says. No official statement what a new driver does for such a client. | not documented officially; hardware check (issue #37) |
| 32 | The meaning of "take-over" for background clients (`port-architecture.md:172, 303`) | not documented in any reachable source; correctly labelled unverified | not documented officially |
| 33 | `~/Library/Preferences/com.3dconnexion.mappings.plist`, "editing is not recommended and not supported" (`docs/windows-mac.md:90`) | forum posts only; no official page reachable | not documented officially (forum) |
| 34 | The driver on macOS needs the 3DconnexionHelper extension enabled (not mentioned anywhere in the repo) | Search summary of the official support article "Enabling 3Dconnexion Extension After Installing the Driver" (3dconnexion.com/us/?p=103119 and locale copies): on macOS 15 and later activate the 3DconnexionHelper extension under General > Login Items & Extensions > Driver Extensions; on macOS 14 and earlier use Privacy & Security > Allow; "navigation may not work, and moving the SpaceMouse cap might cause the cursor to move instead" when not enabled. A moderator calls 3DconnexionHelper the driver daemon, "a user-mode application running headless". | confirmed (official support article, via search summary); **missing from the docs**: added to `windows-mac.md` |
| 35 | "Input Monitoring is not needed for the driver path" (`docs/wiki/Requirements.md:161`) | no official statement found | not documented officially |
| 36 | hidapi seizes the device by default; a shared mode exists (`docs/wiki/Requirements.md:152`) | hidapi `mac/hid.c`: `hid_darwin_set_open_exclusive(1); /* Backward compatibility */`, `device_open_options = (open_exclusive == 0) ? kIOHIDOptionsTypeNone : kIOHIDOptionsTypeSeizeDevice`. The version number "0.12" was not checked. | confirmed (code) |

### Configuration files of the driver

| # | Claim (file:line) | Source and exact fact | Verdict |
|---|---|---|---|
| 37 | User-specific configuration is `%APPDATA%\3Dconnexion\3DxWare\Cfg`, files are XML (`docs/windows-mac.md:66`, `driver_cleanup_model.hpp:6`, `docs/ui.md`) | Search summary of the official 3Dconnexion FAQ "Where can I find my 3DxWare configuration files?" (3dconnexion.com/us/?p=5394): user-specific files are in the `%APPDATA%` `Cfg` folder, extension `.XML`. A forum thread adds `%PROGRAMDATA%\3Dconnexion\3DxWare\Cfg` for machine-wide files and `C:\Program Files\3Dconnexion\3DxWare\3DxWinCore64\Cfg` for installed defaults; log `%LOCALAPPDATA%\3Dconnexion\3DxWare\3DxService.log`. | folder confirmed (official FAQ, via search summary) |
| 38 | Contents of the XML: one file per program, `<AxisBank>`, `<Axis>`, `<Enabled>true\|false</Enabled>`, `<Input>/<Output>/<ActionID>`, `<Signature>/<Name>/<ExecutableName>`, `HIDMultiAxis_*` actions (`driver_cleanup_model.hpp:6-12, 167-189`, `docs/ui.md`, `docs/wiki/User-interface.md`) | no official description of the file format was found; the official FAQ only names the folder | **observed only** (on a real machine), not officially documented: the user docs now say so (see `windows-mac.md`) |
| 39 | `Cfg\dofscroll-backup\...` is a safe place to back up files in the driver folder (`docs/ui.md`) | not documented. The official FAQ "How do I clean up the configuration on Windows?" (3dconnexion.com/lv/?p=5706 and locale copies, via search summary) describes the vendor's own reset: stop the driver (Start > All Apps > 3Dconnexion > Stop 3DxWare), close 3D applications, rename the `3Dconnexion` folder in `%APPDATA%` and in `%LOCALAPPDATA%` to `3Dconnexion.bak`, start the driver again (Start 3DxWare); share the `.bak` folders with support if the problem goes away. | observed only; the official method is different (see PR #352 comment) |
| 40 | Export/import in the driver panel, "only with the same driver version"; the storage location can be moved via the Administrators Guide (`docs/windows-mac.md:70`) | search summary of the official FAQ, as in the file; the Administrators Guide was not read | confirmed (search summary), guide could not check |
| 41 | Process and service names used by the coexistence scripts: `3DxService`, `3DxWinCore`, `3DxWinCore64`, `3dxpiemenus`, `3DxMouseTray`, `3DconnexionHelper` (`scripts/test-driver-coexistence.ps1:188`, `.sh:176`) | search summaries: `3DxService.exe` in `C:\Program Files\3Dconnexion\3DxWare\3DxWinCore64\` (also reported under `3DxWinCore`); `3DconnexionHelper` is the macOS driver daemon. `3dxpiemenus`, `3DxMouseTray`, `3DxWinCore.exe`: no source found. The script only lists matching names for the report; a wrong name costs nothing. | partly confirmed; rest observed/unchecked |
| 42 | The driver has per-application profiles with axis enable/invert/sensitivity, exports, and no documented generic scroll mode (`docs/windows-mac.md:Summary`) | forum and Autodesk help, as labelled in the file; the SpaceMouse Pro manual could not be re-fetched | as labelled in `windows-mac.md` (forum / unresolved) |

## Findings that need a decision or a hardware check

These are contradicted or not settled by an official source and cannot be fixed by documentation
alone. Each has a GitHub issue (numbers in the pull request description).

1. **Signs of the Windows and macOS default mixes against the documented raw convention.**
   `spacenavd` (`dev.c`) turns the raw order X, Y, Z, Rx, Ry, Rz into libspnav by swapping Y/Z and
   Rz/Ry and negating y, z and the swapped rotations. Blender converts the macOS client API values
   `{tx, ty, tz}` to its view frame as `{tx, -tz, ty}` and `{rx, ry, rz}` as `{-rx, rz, -ry}`, and the
   Windows raw report the same way; its Linux (libspnav) path converts `{x, y, -z}` and `{-rx, -ry, rz}`.
   Equating the two shows that the driver client API and the raw report use the **raw** order and
   signs (push away is positive `y`, down is positive `z`). With that, the Linux default
   `z + rx` equals `-y + rx` in raw terms, but the Windows and macOS defaults say `y + rx`; the
   horizontal and zoom mixes show the same kind of difference. Either the `TDxInput` COM values
   have other signs than the raw report (nothing official says), or the Windows file was set to
   what was seen on one machine, and the macOS file (copied from it, "experimental") does not
   hold. Needs a measurement with the axis assistant on a real Windows and a real macOS machine
   (related: issue 250).
2. **Button reports 0x1c / 0x1d.** See row 18. The HID path would show no buttons for newer
   models if they send button numbers in report 0x1c instead of the bitmask in report 3.
3. **USB ids missing from `devices/*.conf`.** `spacenavd` knows `256f:c652` (`VID_3DCONN`/
   `PID_WIRELESS`, the receiver; `dev.c` decides by button count whether it is a SpaceMouse Wireless
   or Pro Wireless) and `256f:c636` (SpaceMouse Module). The HID path (Windows, macOS) matches by
   `vid:pid` only, so a receiver is not recognised. A search summary of a 3Dconnexion forum post gives
   product id 0xC641 for the SpaceMouse Module USB, while `spacenavd` lists 0xc636 as "spacemouse
   module" and blacklists 0xc641 as "scout(?)". Which ids are right needs a device.
4. **Scale and units of the COM interface** (rows 25, 28): `kWinDriverTranslationScale`,
   `kWinDriverRotationScale`, `kMacDriverAxisScale` are placeholders (1.0). The vendor's SDK
   documentation (developer download) may state them; the maintainer would have to read it.
5. **Coexistence** (rows 21, 31, 32, 35): no official statement either way. Stays with the guided
   test (issues 36 and 37).

## Fixes made with this check

- [windows-mac.md](windows-mac.md): link to this file; the macOS driver needs the 3DconnexionHelper
  extension enabled (row 34); the contents of the `Cfg` XML files are observed, not documented
  (row 38); the vendor's own cleanup procedure (row 39); the moderator's Raw Input recommendation
  (row 22).
- [devices.md](devices.md): the button report caveat (row 18) and the spacenavd ids not shipped (row 9).
- [security.md](security.md): the default socket path is spelled `/var/run/spnav.sock` by libspnav and
  the client tries other paths first (row 7).
