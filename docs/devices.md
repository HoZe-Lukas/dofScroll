<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Device database and device description files

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

Plain text, one `key = value` per line, `#` starts a comment. Limits: at most 64 `*.conf`
files per directory, 16 KiB per file, regular files only (symlinks are refused,
`O_NOFOLLOW`).

| Key | Meaning |
|---|---|
| `name` | Display name (required, max 128 characters) |
| `id` | `vid:pid` as 16-bit numbers (decimal or `0x` hex); repeatable up to 16 times (required) |
| `factory_buttons` | Physical button count, 0..64 (optional; must not exceed `bits`) |
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
