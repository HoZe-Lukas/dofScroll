# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Starts dofscroll.exe at every logon of the current user (no administrator rights needed).

.DESCRIPTION
  Default: writes the value 'dofscroll' under
  HKCU\Software\Microsoft\Windows\CurrentVersion\Run (quoted path to dofscroll.exe).
  With -Startup a shortcut in the user's Startup folder is created instead.
  Idempotent: running it again replaces the entry. It only touches the current user and the one
  entry named by -Name. -WhatIf (or -DryRun) prints what would be done without changing anything.

.PARAMETER ExePath
  Full path of dofscroll.exe. Default: dofscroll.exe next to this script, or in the parent folder.

.PARAMETER Name
  Name of the Run value / shortcut. Default: dofscroll.

.PARAMETER Startup
  Use a shortcut in the Startup folder instead of the Run key.

.PARAMETER DryRun
  Same as -WhatIf.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install-autostart.ps1 -ExePath "C:\Program Files\dofscroll\dofscroll.exe"
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$ExePath,
    [string]$Name = 'dofscroll',
    [switch]$Startup,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($DryRun) { $WhatIfPreference = $true }

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

if (-not $ExePath) {
    foreach ($c in @((Join-Path $PSScriptRoot 'dofscroll.exe'), (Join-Path (Split-Path $PSScriptRoot -Parent) 'dofscroll.exe'))) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { $ExePath = $c; break }
    }
}
if (-not $ExePath) { throw 'dofscroll.exe not found: pass -ExePath' }
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) { throw "not a file: $ExePath" }
$ExePath = (Resolve-Path -LiteralPath $ExePath).ProviderPath
if ($Name -notmatch '^[A-Za-z0-9._-]{1,64}$') { throw "invalid -Name '$Name' (letters, digits, . _ - only)" }

if ($Startup) {
    $dir = [Environment]::GetFolderPath('Startup')
    $lnk = Join-Path $dir ($Name + '.lnk')
    if ($PSCmdlet.ShouldProcess($lnk, "create shortcut to $ExePath")) {
        $sh = New-Object -ComObject WScript.Shell
        $s = $sh.CreateShortcut($lnk)
        $s.TargetPath = $ExePath
        $s.WorkingDirectory = Split-Path $ExePath -Parent
        $s.Description = 'dofscroll SpaceMouse daemon'
        $s.Save()
    }
    Write-Output "autostart: shortcut $lnk -> $ExePath"
} else {
    $value = '"' + $ExePath + '"'
    if ($PSCmdlet.ShouldProcess("$runKey\$Name", "set value $value")) {
        if (-not (Test-Path $runKey)) { New-Item -Path $runKey -Force | Out-Null }
        Set-ItemProperty -Path $runKey -Name $Name -Value $value -Type String
    }
    Write-Output "autostart: $runKey\$Name = $value"
}
