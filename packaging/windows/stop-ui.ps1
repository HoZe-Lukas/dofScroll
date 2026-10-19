# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Stops the dofscroll interface (dofscroll-ui.exe) of the current user, e.g. before replacing it.

.DESCRIPTION
  Only processes of the current session whose executable path is exactly -ExePath are touched.
  The window is asked to close first; the tray icon keeps the process alive, so after -TimeoutSeconds
  the process is ended with Stop-Process. The interface holds no keys and no state that needs a
  clean shutdown (its settings are written when changed).
  Exit code: 10 if at least one process was stopped (the installer then starts the UI again after
  an interactive update), 0 if none was running.

.PARAMETER ExePath
  Full path of dofscroll-ui.exe. Default: dofscroll-ui.exe in the parent folder of this script
  (installed layout: <install dir>\scripts\stop-ui.ps1), or next to it.

.PARAMETER TimeoutSeconds
  How long to wait after asking the window to close. Default 3.

.PARAMETER DryRun
  Same as -WhatIf.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$ExePath,
    [ValidateRange(1, 60)][int]$TimeoutSeconds = 3,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($DryRun) { $WhatIfPreference = $true }

if (-not $ExePath) {
    foreach ($c in @((Join-Path (Split-Path $PSScriptRoot -Parent) 'dofscroll-ui.exe'), (Join-Path $PSScriptRoot 'dofscroll-ui.exe'))) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { $ExePath = $c; break }
    }
}
if (-not $ExePath) {
    Write-Output 'stop-ui: dofscroll-ui.exe not found; nothing to do'
    exit 0
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)

# Process.Path is empty when this script runs in 32-bit PowerShell (the NSIS installer starts that one)
# and looks at a 64-bit process; the WMI/CIM executable path works from both.
function Get-ProcPath($p) {
    if ($p.Path) { return $p.Path }
    try { return (Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction Stop).ExecutablePath } catch { return $null }
}

$sid = (Get-Process -Id $PID).SessionId
$procs = @(Get-Process -Name 'dofscroll-ui' -ErrorAction SilentlyContinue | Where-Object {
    $_.SessionId -eq $sid -and ($pp = Get-ProcPath $_) -and ([string]::Equals($pp, $ExePath, [System.StringComparison]::OrdinalIgnoreCase))
})
$stopped = 0
foreach ($p in $procs) {
    if (-not $PSCmdlet.ShouldProcess("dofscroll-ui.exe (pid $($p.Id))", 'stop')) { continue }
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $p.Id -Force
        $null = $p.WaitForExit(5000)
    }
    $stopped++
}
Write-Output "stop-ui: $stopped process(es)"
if ($stopped -gt 0) { exit 10 }
exit 0
