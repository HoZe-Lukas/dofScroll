<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Windows packaging files (experimental)

There is no installer yet (package W4). These scripts are for a manual setup from the build
artifact (`dofscroll.exe`, `devices\`, `examples\` next to each other). None needs administrator
rights; all act on the current user only.

| Script | What it does |
|---|---|
| `install-autostart.ps1 -ExePath <path>` | Starts the daemon at logon: value `dofscroll` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. `-Startup` creates a shortcut in the Startup folder instead. Idempotent. |
| `uninstall-autostart.ps1` | Removes the Run value and/or the Startup shortcut. |
| `stop-ui.ps1` | Stops the interface (`dofscroll-ui.exe`) at `-ExePath` (current session only); exit code 10 if one was running. The installer uses it before an update and starts the UI again afterwards (interactive installs only). |
| `stop-daemon.ps1` | Stops the daemon at `-ExePath` (current session only): asks it to quit through its event, ends it after a timeout, then runs `--release-keys`; exit code 10 if one was running. The installer uses it before an update and starts the daemon again afterwards (also for `/S`). |

All three accept `-WhatIf` (or `-DryRun`) to print the actions without doing them.

```powershell
powershell -ExecutionPolicy Bypass -File install-autostart.ps1 -ExePath "C:\Tools\dofscroll\dofscroll.exe"
```

Why the Run key and not a service or a scheduled task: input injection needs the interactive
user session, so a Windows service is unsuitable; the Run key needs no elevation and is removed
cleanly. A scheduled task "at logon" would also work but is not provided. Behavior across fast
user switching is unverified.

CI (`.github/workflows/ports.yml`, Windows jobs) runs a real install, read-back and uninstall
round trip against `HKCU` of the runner and parses every script. That checks the scripts, not
that Windows actually starts the daemon at logon (a human check, see
`docs/port-windows-checklist.md`).
