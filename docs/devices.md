<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Device database and device description files

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons and the vendor driver](modes.md#buttons-and-the-vendor-driver). Button remapping is done in the driver, not in the device files.

Two independent pieces (neither is wired into the daemon yet):

1. **Factory button counts** (`src/device_db.cpp`): `factoryButtonCount(deviceName, reported)`
   returns the physical button count of a known model, matched as a case-insensitive
   substring of the reported device name. Unknown models return `reported` unchanged.
2. **Device description files** (`devices/*.conf`, parser in `src/hid_descriptor.*`): describe
   how a raw HID report maps to axes and buttons, so further devices can be added without
   recompiling. The format is the same on every OS.

## Verified button counts

| Model | Buttons | Basis |
|---|---|---|
| SpaceMouse Compact | 2 | 3Dconnexion product page |
| SpaceMouse Wireless | 2 | 3Dconnexion product page |
| SpaceMouse Pro | 15 | 3Dconnexion product page (15 programmable keys) |
| SpaceMouse Pro Wireless | 15 | 3Dconnexion buy page (15 programmable keys) |
| SpaceMouse Enterprise | 31 | Manufacturer feature text repeated by retailers (31 programmable keys); one retailer lists 15 |
| SpaceNavigator | 2 | Manufacturer product copy as published by distributors |

SpaceExplorer and SpacePilot Pro are deliberately not listed: no count could be verified.
USB vendor/product IDs come from the spacenavd device table. The report layout used in the
shipped files (report 1 = translation, 2 = rotation, 3 = buttons, signed 16-bit little endian
per axis) is the commonly used 3Dconnexion layout and has **not** been verified on hardware.
For Pro, Pro Wireless and Enterprise the raw button bits are not numbered sequentially
(spacenavd remaps them), so their `buttons` line is a placeholder.

## File format

Plain text, one `key = value` per line, `#` or `;` starts a comment, but only at the start of a line or after whitespace; the value of `name` is taken verbatim (only whole-line comments), so `name = Foo #2` keeps `#2`. Limits: at most 64 `*.conf`
files per directory, 16 KiB per file, regular files only (symlinks are refused,
`O_NOFOLLOW`).

| Key | Meaning |
|---|---|
| `name` | Display name (required, max 128 characters) |
| `id` | `vid:pid` as 16-bit numbers (decimal or `0x` hex); repeatable up to 16 times (required) |
| `factory_buttons` | Physical button count, 0..32 (optional; requires a `buttons` line and must not exceed its `bits`) |
| `axis.<a>` | `a` is one of `tx ty tz rx ry rz`; options below; each axis at most once |
| `buttons` | `report=<id> offset=<n> bits=<1..32>`; at most once |

Axis options (`key=value`, space separated): `report` (0..255), `offset` (byte offset, 0..255),
`type` (`s8 u8 s16le s16be u16le u16be s32le`), optional `scale` (finite, non-zero, default 1).
The decoded value is `raw * scale`.

Offsets count bytes **after** the report-id byte; byte 0 of every raw report is the report id.
Buttons: `bits` bits are read little-endian starting at `offset`; bit *i* (LSB first) is
button *i*. At least one axis or the `buttons` line must be present.

Example:

```
name = SpaceMouse Compact
id = 0x256f:0xc635
factory_buttons = 2
axis.tx = report=1 offset=0 type=s16le
axis.ty = report=1 offset=2 type=s16le
axis.tz = report=1 offset=4 type=s16le
axis.rx = report=2 offset=0 type=s16le
axis.ry = report=2 offset=2 type=s16le
axis.rz = report=2 offset=4 type=s16le
buttons = report=3 offset=0 bits=2
```

## API

`parseHidDescriptor`, `loadHidDescriptorFile` and `loadHidDescriptorDir` read descriptions;
`decodeHidReport(desc, bytes, len, out)` is a pure function that returns the axes (and which
of them were present in that report) and the button bitmask. Short or foreign-id reports
decode nothing and return false; axes that do not fit into a short report are skipped.

## Verifying a device with hidprobe

The shipped `devices/*.conf` layouts have not been verified on hardware. `dofscroll-hidprobe`
(built by default on Linux, option `-DDOFSCROLL_HIDPROBE=OFF` to skip, installed to `bin`) lets
you check them. It only reads `/dev/hidraw*` and never writes to the device.

```sh
dofscroll-hidprobe --list                 # devices, vid:pid, and whether a devices/*.conf entry matches
dofscroll-hidprobe --descriptor           # report descriptor as hex plus a short summary
dofscroll-hidprobe --dump --seconds 10    # raw reports with timestamps and report ids
dofscroll-hidprobe --check                # live decoded axes/buttons, verdict at the end
dofscroll-hidprobe --guided               # asks you to push, tilt, twist, press; writes hidprobe-report.txt
```

Options: `--device /dev/hidrawN` (default: first device with a description), `--devices-dir DIR`
or `--conf FILE` (default: `./devices`, then the installed share directory), `--seconds N`
(`0` = until Ctrl-C), `--report FILE`. Run it from the source tree so `./devices` is found.

`--check` prints one verdict per check: report ids seen, report lengths against the lengths the
description needs (and against the device's own report descriptor), axes that moved with the
observed minimum and maximum against the configured type and scale, button bits seen, and the
highest bit against `factory_buttons`. Axes you did not move and buttons you did not press are
reported as warnings, not failures: move everything to its limits and press every button.

`--guided` produces `hidprobe-report.txt` with the device name, USB id, report descriptor hex,
the observed layout and all mismatches. The serial number is replaced by a short hash, and the
home directory and user name are removed. Read it once, then paste it into an issue.

### Permissions

`/dev/hidraw*` is usually readable by root only. Either run once with `sudo`, or add a udev rule
that gives the logged-in user read access (replace `046d` with your vendor id from `--list`;
3Dconnexion uses `046d` and `256f`):

```sh
echo 'KERNEL=="hidraw*", ATTRS{idVendor}=="046d", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/71-dofscroll-hidprobe.rules
sudo udevadm control --reload && sudo udevadm trigger
```

Then unplug and replug the device. hidprobe never changes permissions itself.

### spacenavd and missing reports

`spacenavd` may keep running: several processes can read the same hidraw node. If no reports
arrive, a device often exposes several hidraw nodes; try the others of the same device with
`--device`. If that also fails, stop `spacenavd` briefly, check the cable or receiver, and run
again. Unrecognised or mismatching results are best reported with the `--guided` file.

### Other platforms

The platform code is one small interface (`tools/hidprobe/hidprobe_backend.hpp`: list devices,
read the report descriptor, read raw reports). A Windows (hid.dll) or macOS (IOHID) backend only
has to implement it; only the Linux backend exists so far.
