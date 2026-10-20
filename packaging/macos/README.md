<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# macOS packaging (experimental)

Files for running the dofscroll daemon as a per-user LaunchAgent. There is no installer, `.pkg`
or signed app bundle yet (work package W4); everything here works from a local build.

| File | Purpose |
|---|---|
| `io.github.dofscroll.daemon.plist.in` | LaunchAgent template (`@DAEMON_PATH@` is substituted) |
| `install-launchagent.sh` | Renders the plist into `~/Library/LaunchAgents`, runs `launchctl bootstrap` |
| `uninstall-launchagent.sh` | `launchctl bootout`, removes the plist (config and logs stay) |

## Install

```sh
cmake -S . -B build && cmake --build build -j4     # builds build/dofscroll
sh packaging/macos/install-launchagent.sh --daemon "$PWD/build/dofscroll"
```

Options: `--dry-run` prints the rendered plist and the `launchctl` calls without changing
anything, `--no-load` only writes the plist. The script is idempotent and never needs `sudo`.
The label is `io.github.dofscroll.daemon`; check with
`launchctl print gui/$(id -u)/io.github.dofscroll.daemon`.

The agent starts at login (`RunAtLoad`) and is restarted after a crash but not after a clean
exit (`KeepAlive` / `SuccessfulExit=false`, 10 s throttle). `LimitLoadToSessionType=Aqua`: it runs
only in a graphical login session, because it needs that session to post events.

## Permissions (required, granted by you)

macOS asks for two permissions per binary. Grant both in **System Settings > Privacy & Security**:

1. **Input Monitoring** - needed to read the SpaceMouse (IOHID). Add the `dofscroll` binary
   with the `+` button, or answer the prompt when it appears, and switch it on.
2. **Accessibility** - needed to post scroll, key and mouse events (`CGEventPost`).

The grant is tied to the binary's **path and code signature**. Moving the binary, rebuilding an
unsigned binary (the signature changes each build) or replacing it with a differently signed one
can invalidate the grant; the symptom is a running daemon that produces no input or no output.
After a rebuild remove the old entry, add the binary again, then restart the agent:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.dofscroll.daemon
```

The exact wording of the panes differs between macOS versions (unverified for each version).

## Logs and files

- Daemon log (rolling): `~/Library/Caches/dofscroll/dofscroll.log`, status file next to it.
- launchd's stdout/stderr go to `/dev/null` (unbounded capture files); the rolling daemon log is the record.
- Configuration: `~/Library/Application Support/dofscroll/` (same format as on Linux).

## Uninstall

```sh
sh packaging/macos/uninstall-launchagent.sh
```

## Vendor driver

dofscroll is meant to run next to the official 3Dconnexion driver, which you keep installed and running.
If both cannot read the device at the same time on your Mac, that is a bug of dofscroll to be fixed.
Behaviour with the driver running is unverified; the guided test (`diagnostics/test-driver-coexistence.sh`
in the app, `dofscroll-hidprobe` next to the daemon) finds out and writes one report file for the
issue tracker. See `docs/troubleshooting.md` and `docs/port-macos.md`.
