# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Stops the dofscroll daemon of the current user (e.g. before replacing dofscroll.exe).

.DESCRIPTION
  Ends only dofscroll.exe processes of the current session. A process that is ended this way
  cannot release keys it holds: run 'dofscroll.exe --release-keys' afterwards (if your build
  supports it) or press and release Ctrl and the mouse buttons once.

.PARAMETER DryRun
  Same as -WhatIf.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param([switch]$DryRun)

$ErrorActionPreference = 'Stop'
if ($DryRun) { $WhatIfPreference = $true }

$sid = (Get-Process -Id $PID).SessionId
$procs = @(Get-Process -Name 'dofscroll' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
foreach ($p in $procs) {
    if ($PSCmdlet.ShouldProcess("dofscroll.exe (pid $($p.Id))", 'stop')) {
        Stop-Process -Id $p.Id
        $null = $p.WaitForExit(5000)
    }
}
Write-Output "stop-daemon: $($procs.Count) process(es)"
