# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Removes what install-autostart.ps1 created (Run value and/or Startup shortcut) for the current user.

.PARAMETER Name
  Name of the Run value / shortcut. Default: dofscroll.

.PARAMETER DryRun
  Same as -WhatIf.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$Name = 'dofscroll',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($DryRun) { $WhatIfPreference = $true }
if ($Name -notmatch '^[A-Za-z0-9._-]{1,64}$') { throw "invalid -Name '$Name' (letters, digits, . _ - only)" }

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$removed = 0

if (Get-ItemProperty -Path $runKey -Name $Name -ErrorAction SilentlyContinue) {
    if ($PSCmdlet.ShouldProcess("$runKey\$Name", 'remove value')) {
        Remove-ItemProperty -Path $runKey -Name $Name
    }
    $removed++
}
$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) ($Name + '.lnk')
if (Test-Path -LiteralPath $lnk -PathType Leaf) {
    if ($PSCmdlet.ShouldProcess($lnk, 'remove shortcut')) {
        Remove-Item -LiteralPath $lnk
    }
    $removed++
}
Write-Output "autostart: removed $removed entr$(if ($removed -eq 1) { 'y' } else { 'ies' })"
