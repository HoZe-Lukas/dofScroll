<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# dofscroll 0.1.0-beta.2

Pre-release (beta). Not a stable release. This is a fix release for
[0.1.0-beta.1](release-notes-0.1.0-beta.1.md); everything in those notes still applies,
including the "Tested on" table and the known limitations.

## Fixed

- **`dofscroll-ui.service` exited immediately** (status 0, no window, no tray) when started by
  systemd, so start at login did not work with the Qt interface on Linux. The single-instance
  lock lived in `~/.local/share/dofscroll`, which the unit sandbox mounts read-only. The lock now
  lives in the per-user runtime directory, a lock error other than "already running" is
  reported with a non-zero exit status, and a stale lock after a kill or crash is recovered.
  See [troubleshooting.md](troubleshooting.md) if you used the beta.1 workaround drop-in.
- `dofscroll-ui --help` and `-h` print the usage instead of starting a second instance.

## Added

- Code of conduct (`CODE_OF_CONDUCT.md`).
- Install smoke test in CI: the Linux packages are installed in fresh containers and started.
  Results per distribution are in [installation.md](installation.md); anything not listed there
  is untested.
- **Tray items "Restart daemon" and "Restart interface"** (Qt interface, all OSes), next to
  "Release all keys". "Restart daemon" releases held keys first, then restarts the daemon (systemd
  on Linux, launchd on macOS, stop and start on Windows). "Restart interface" restarts the
  interface itself; a restarted instance waits for the old one's single-instance lock. See
  [ui.md](ui.md).
- **`dofscroll-ui.service` has `Wants=dofscroll.service`**: starting only the interface starts
  the daemon too.
- **Windows installer** stops a running `dofscroll-ui.exe` of the install before replacing files
  and starts it again after an interactive update (new script `scripts\stop-ui.ps1`).
- **Linux packages** restart the running user units (`dofscroll.service`, `dofscroll-ui.service`,
  `dofscroll-tray.service`) of logged-in users after an upgrade, best effort: RPM `%post` / DEB
  `postinst`, upgrade only, never fails the transaction, never touches the configuration.
- **macOS and upgrade docs:** upgrade steps per OS in [installation.md](installation.md#upgrade).

## Upgrading from 0.1.0-beta.1

Install the new package over the old one. If you added a drop-in
`~/.config/systemd/user/dofscroll-ui.service.d/override.conf` as a workaround, remove it, then run
`systemctl --user daemon-reload` and restart the units. Configuration is never touched by packages.

## Known limitations

Unchanged from beta.1: Windows and macOS were never run with a real device, the Qt interface
was not checked on Windows or macOS desktops, packages are verified only on the distributions
listed in the installation guide. Open work is tracked in the
[issues](https://github.com/HoZe-Lukas/dofScroll/issues).

## Checksums

Verify downloads with `sha256sum -c --ignore-missing SHA256SUMS.txt` (protects against damaged
downloads, not against a compromised release).
