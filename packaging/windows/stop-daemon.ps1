# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Stops the dofscroll daemon of the current user (e.g. before replacing dofscroll.exe).

.DESCRIPTION
  Only processes of the current session whose executable path is exactly -ExePath are touched
  (not every process named dofscroll.exe). Steps:
  1. Ask the daemon to quit through its named event Local\dofscroll-quit; it releases the keys it
     holds itself and exits. Wait up to -TimeoutSeconds.
  2. If it is still running, end it with Stop-Process (it cannot release held keys then).
  3. Run 'dofscroll.exe --release-keys' to release anything that may be stuck.

.PARAMETER ExePath
  Full path of dofscroll.exe. Default: dofscroll.exe next to this script, or in the parent folder.

.PARAMETER TimeoutSeconds
  How long to wait for the daemon after asking it to quit. Default 5.

.PARAMETER DryRun
  Same as -WhatIf.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$ExePath,
    [ValidateRange(1, 60)][int]$TimeoutSeconds = 5,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($DryRun) { $WhatIfPreference = $true }

if (-not $ExePath) {
    foreach ($c in @((Join-Path $PSScriptRoot 'dofscroll.exe'), (Join-Path (Split-Path $PSScriptRoot -Parent) 'dofscroll.exe'))) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { $ExePath = $c; break }
    }
}
if (-not $ExePath) {
    if ($WhatIfPreference) { Write-Output 'stop-daemon: dofscroll.exe not found (pass -ExePath); nothing to do'; return }
    throw 'dofscroll.exe not found: pass -ExePath'
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)

# Process.Path is empty when this script runs in 32-bit PowerShell (the NSIS installer starts that one)
# and looks at a 64-bit process; the WMI/CIM executable path works from both.
function Get-ProcPath($p) {
    if ($p.Path) { return $p.Path }
    try { return (Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction Stop).ExecutablePath } catch { return $null }
}

$sid = (Get-Process -Id $PID).SessionId
function Get-Daemon {
    @(Get-Process -Name 'dofscroll' -ErrorAction SilentlyContinue | Where-Object {
        $_.SessionId -eq $sid -and ($pp = Get-ProcPath $_) -and ([string]::Equals($pp, $ExePath, [System.StringComparison]::OrdinalIgnoreCase))
    })
}

$procs = Get-Daemon
if ($procs.Count -eq 0) {
    # help to find out why nothing matched (shown in the installer log)
    Write-Output "stop-daemon: expected path '$ExePath', session $sid"
    Get-Process -Name 'dofscroll' -ErrorAction SilentlyContinue | ForEach-Object { Write-Output "stop-daemon: saw pid $($_.Id) session $($_.SessionId) path '$(Get-ProcPath $_)'" }
}
foreach ($p in $procs) {
    if (-not $PSCmdlet.ShouldProcess("dofscroll.exe (pid $($p.Id))", 'stop')) { continue }
    # 1. polite request through the quit event (same user only: the event is private to the user)
    try {
        $ev = [System.Threading.EventWaitHandle]::OpenExisting('Local\dofscroll-quit')
        try { $null = $ev.Set() } finally { $ev.Dispose() }
    } catch {
        Write-Verbose "quit event not available: $($_.Exception.Message)"
    }
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        # 2. fallback: end it
        Write-Warning "pid $($p.Id) did not quit within $TimeoutSeconds s; ending it"
        Stop-Process -Id $p.Id -Force
        $null = $p.WaitForExit(5000)
    }
}

# 3. release anything that may still be held (a forced end cannot release keys)
if ($procs.Count -gt 0 -and (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    if ($PSCmdlet.ShouldProcess($ExePath, '--release-keys')) {
        $null = Start-Process -FilePath $ExePath -ArgumentList '--release-keys' -Wait -WindowStyle Hidden
    }
}
Write-Output "stop-daemon: $($procs.Count) process(es)"
# exit code 10: a daemon was running (the installer starts it again after an update)
if ($procs.Count -gt 0) { exit 10 }
exit 0
