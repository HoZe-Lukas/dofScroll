<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Devices and verifying a device

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

Supported on Linux: models that `spacenavd` supports. Tested on hardware: SpaceMouse Pro and SpaceMouse Compact (RHEL 9, X11, KDE Plasma). All other models are untested. Full text: [docs/devices.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/devices.md). Button counts: [Buttons](Buttons).

## Device description files

`devices/*.conf` describe how a raw HID report maps to axes and buttons, so more devices can be added without recompiling. The format is the same on every OS; the parser is [src/hid_descriptor.cpp](https://github.com/HoZe-Lukas/dofScroll/blob/main/src/hid_descriptor.cpp). Shipped files:

`spacemouse-compact`, `spacemouse-wireless`, `spacemouse-pro`, `spacemouse-pro-wireless`, `spacemouse-enterprise`, `spacenavigator` (in [devices/](https://github.com/HoZe-Lukas/dofScroll/tree/main/devices)).

Keys: `name`, `id` (`vid:pid`, repeatable), `factory_buttons`, `axis.<tx|ty|tz|rx|ry|rz>`, `buttons`. Example:

```
name = SpaceMouse Compact
id = 0x256f:0xc635
factory_buttons = 2
axis.tx = report=1 offset=0 type=s16le
buttons = report=3 offset=0 bits=2
```

The report layout in the shipped files is the commonly used 3Dconnexion layout and has **not been verified on hardware**. For Pro, Pro Wireless and Enterprise the raw button bits are not numbered sequentially, so their `buttons` line is a placeholder. On Linux the daemon gets its data from `spacenavd`; the Windows and macOS ports use these files.

## Verifying a device with hidprobe

`dofscroll-hidprobe` (Linux, built by default, option `-DDOFSCROLL_HIDPROBE=OFF` to skip) reads `/dev/hidraw*` and never writes to the device.

```sh
dofscroll-hidprobe --list                 # devices, vid:pid, matching description
dofscroll-hidprobe --descriptor           # report descriptor as hex plus summary
dofscroll-hidprobe --dump --seconds 10    # raw reports with timestamps
dofscroll-hidprobe --check                # live decoded axes/buttons, verdict at the end
dofscroll-hidprobe --guided               # guided run; writes hidprobe-report.txt
```

`--check` compares report ids, lengths, axis ranges and button bits against the description. Move every axis to its limits and press every button; unmoved axes are warnings, not failures. `--guided` writes `hidprobe-report.txt`; the serial number is replaced by a short hash and the home directory and user name are removed. Read it, then attach it to an issue.

`/dev/hidraw*` is usually readable by root only: run once with `sudo`, or add a udev rule with `TAG+="uaccess"` for your vendor id (3Dconnexion uses `046d` and `256f`), see the [full guide](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/devices.md#permissions). If no reports arrive, try the other hidraw nodes of the same device with `--device`.

Verifying descriptors and button counts on real hardware is an open work item, see [Roadmap](Roadmap).
