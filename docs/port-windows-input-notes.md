<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Windows HID input: integration notes

For whoever wires `src/platform/win/win_hid_input.cpp` into the daemon (`cmake/windows.cmake`,
`win_platform.cpp`). Hardware behavior is unverified.

- **Link:** `setupapi hid advapi32 bcrypt` (the last two come with the core library). `hidsdi.h` /
  `hidpi.h` are included inside `extern "C"` because MinGW-w64's headers have no C linkage guard
  (the link failed with mangled names without it).
- **Factory:** `makeWinHidInput(descriptorDir, &log)`. `descriptorDir` must contain the
  `devices/*.conf` files (install them next to the executable, e.g. `<exe dir>/devices`). `open()`
  fails (the daemon core retries with backoff) when no description loads; it succeeds with zero
  devices attached and picks them up by rescan.
- **Hot-plug:** a manager thread rescans the HID class every 2 s. A reader error (unplug, sleep/wake)
  drops the device (buttons are released first), the next rescan reopens it.
- **One DeviceInfo per physical device:** interfaces are grouped by container id and one is chosen
  (`pickInterfaces`, `win_hid_path.hpp`): generic-desktop multi-axis controller (0x01/0x08) first,
  keyboard/mouse/consumer/system-control usages never. Which usage each model really uses is
  unverified; if a model delivers nothing, check the log lines `HID interface ...`.
- **Vendor driver:** devices are opened read-only and shared (`FILE_SHARE_READ|WRITE`). Whether the
  3Dconnexion driver allows a second reader, or opens the device exclusively (then `CreateFile`
  reports a sharing violation, logged and retried every 2 s), is unverified. Stop the vendor
  service while testing.
- **Logging:** hot-plug messages go to `std::cerr` with the `[dofscroll] ` prefix, names through
  `sanitizeForLog`; the daemon's log tee picks them up.
- **Tests:** `tests/test_win_hid_path.cpp` (pure, every OS) covers path parsing, rescan diff and
  interface picking. The Win32 calls themselves are only compile-checked (MinGW) and smoke-run under
  Wine (`open()` with no devices, `wait()`, double `close()`); no HID device is attached there.
