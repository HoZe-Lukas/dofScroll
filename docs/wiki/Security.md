<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Security

Full documents: [docs/security.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/security.md), [SECURITY.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/SECURITY.md), and the written review [docs/security-review-2026-10.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/security-review-2026-10.md).

## Reporting a vulnerability

Report privately through GitHub private vulnerability reporting: the repository's **Security** tab, then **Report a vulnerability** ([direct link](https://github.com/HoZe-Lukas/dofScroll/security/advisories/new)). Do not open a public issue or pull request and do not post details in discussions. GitHub is the only contact channel. This is a spare-time project: the goal is to acknowledge within about a week, there is no SLA and no bug bounty. Only the latest release and `main` get fixes (the project is pre-1.0). Vulnerabilities in `spacenavd`, `libspnav`, Qt, the X server or the kernel belong to those projects.

## What dofscroll is and is not

dofscroll injects keyboard and mouse input into your session, so it protects the integrity of input and the availability of the desktop (no stuck keys, no event floods). The threat model considers other local users, a restricted process of the same user, a prepared USB device, and a manipulated config or mode file.

- **No root.** The daemon and the tray run as the user (systemd user units).
- **systemd sandbox.** The user units are hardened (`NoNewPrivileges`, `ProtectSystem=strict`, syscall filter, address-family restriction and more). It is an accident-containment measure, not a security boundary.
- **uinput is opt-in.** `/dev/uinput` acts below the display server, so anyone who can write to it can inject input into every session at the seat. The packages ship the udev rule only as an example. Do not use the `input` group, it exposes all keyboards. See [packaging/README-uinput.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/packaging/README-uinput.md).
- **Mode files are code-like.** They can press any key from the key list, including modifier chords. Read them before use, see [Modes](Modes).
- **Parser limits.** Config, mode and device files have size and count limits and invalid values are rejected.
- **Windows file layer.** The Windows file-system layer refuses reparse points and hazardous paths (documented in section 9 of docs/security.md; unverified on hardware).

## Known limitations (0.1)

From the October 2026 review, kept open for 0.1:

- Keys held through XTest are not released if the daemon is killed with SIGKILL; there is no watchdog for a stalled tick. `dofscroll --release-keys` fixes it by hand.
- Config and mode loaders do not reject group- or world-writable files. Keep `~/.config/dofscroll` private (`0700`).
- The daemon unit may write the whole `$XDG_RUNTIME_DIR`.
- The tray sandbox cannot save a config that is a symlink into another directory.
- Example files are copied from a directory chosen by environment or by the executable's location.
- Runtime temporary file names are predictable (needs write access to the user's own runtime directory).

The repository also runs a secret scan in the pre-commit hook and CI, pinned GitHub Actions, and a lint workflow. See [Development](Development).
