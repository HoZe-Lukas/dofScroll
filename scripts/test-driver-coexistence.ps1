# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Guided test (about 5 minutes): can dofscroll read the SpaceMouse at the same time as the official
  3Dconnexion driver? Writes one text file to attach to an issue.

.DESCRIPTION
  dofscroll is meant to run next to the official driver (3DxWare / 3Dconnexion Software). Whether both
  can read the device at the same time has NOT been verified on real hardware yet. This script finds
  out and writes dofscroll-driver-test-report.txt in the current folder.

  Test A  reads the device with dofscroll-hidprobe while the driver runs (you move the cap for 10 s).
  Test B  starts dofscroll next to the driver and asks you a few yes/no questions.
  Test C  OPTIONAL, for diagnosis only, normal users do not need it: the same as A with the driver
          stopped. The script never stops anything itself; it only tells you how if you want it.

  The script changes nothing: it does not stop, kill or configure the driver. The only process it may
  stop is the dofscroll it started itself in Test B (through the same event stop-daemon.ps1 uses).
  The report contains no user name and no personal paths (the home folder is written as ~).

.PARAMETER ProbePath   Path of dofscroll-hidprobe.exe (default: next to dofscroll.exe, or in PATH).
.PARAMETER DaemonPath  Path of dofscroll.exe (default: next to dofscroll-hidprobe.exe).
.PARAMETER ReportPath  Output file (default: dofscroll-driver-test-report.txt in the current folder).
.PARAMETER Seconds     How long the cap is moved in a test (default 10).
.PARAMETER NonInteractive  No questions, no waiting for the user: all questions are answered "skipped"
                           (used by the installer test on the CI runner; also a quick self-check).
#>
[CmdletBinding()]
param(
    [string]$ProbePath = '',
    [string]$DaemonPath = '',
    [string]$ReportPath = '',
    [ValidateRange(2, 120)][int]$Seconds = 10,
    [switch]$NonInteractive
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Continue'

$script:Lines = New-Object System.Collections.ArrayList
$script:Verdicts = New-Object System.Collections.ArrayList

function Add-Line([string]$Text) { [void]$script:Lines.Add($Text) }
function Add-Verdict([string]$Text) { [void]$script:Verdicts.Add($Text); Write-Host ('  => ' + $Text) }
function Add-Section([string]$Title) {
    Add-Line ''
    Add-Line ('=== ' + $Title)
    Write-Host ''
    Write-Host ('=== ' + $Title)
}

# Returns 'yes', 'no' or 'skipped'.
function Read-YesNo([string]$Question) {
    if ($NonInteractive) { Add-Line ('Q: ' + $Question + ' -> skipped (non-interactive)'); return 'skipped' }
    while ($true) {
        $a = (Read-Host ($Question + ' [y/n/s=skip]')).Trim().ToLowerInvariant()
        if ($a -eq 'y' -or $a -eq 'yes') { Add-Line ('Q: ' + $Question + ' -> yes'); return 'yes' }
        if ($a -eq 'n' -or $a -eq 'no') { Add-Line ('Q: ' + $Question + ' -> no'); return 'no' }
        if ($a -eq 's' -or $a -eq 'skip' -or $a -eq '') { Add-Line ('Q: ' + $Question + ' -> skipped'); return 'skipped' }
    }
}

function Wait-Enter([string]$Message) {
    if ($NonInteractive) { return }
    [void](Read-Host $Message)
}

function Find-FirstFile([string[]]$Candidates) {
    foreach ($c in $Candidates) {
        if ($c -and (Test-Path -LiteralPath $c -PathType Leaf)) { return (Resolve-Path -LiteralPath $c).ProviderPath }
    }
    return ''
}

# Runs a program, waits at most $TimeoutSec, returns @{ ExitCode; Output; TimedOut }.
function Invoke-Capture([string]$File, [string[]]$ArgList, [int]$TimeoutSec) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $File
    $quoted = @()
    foreach ($a in $ArgList) {
        if ($a -match '[\s"]') { $quoted += ('"' + ($a -replace '"', '\"') + '"') } else { $quoted += $a }
    }
    $psi.Arguments = ($quoted -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $res = @{ ExitCode = -1; Output = ''; TimedOut = $false }
    try {
        $p = [System.Diagnostics.Process]::Start($psi)
    } catch {
        $res.Output = 'could not start: ' + $_.Exception.Message
        return $res
    }
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        $res.TimedOut = $true
        try { $p.Kill() } catch { }   # our own short-lived hidprobe child only
        [void]$p.WaitForExit(2000)
    }
    $res.ExitCode = if ($p.HasExited) { $p.ExitCode } else { -1 }
    $res.Output = ($outTask.Result + "`n" + $errTask.Result)
    return $res
}

# Keeps the useful lines of hidprobe output (drops the live "tx=... " refresh lines).
function Get-ProbeLines([string]$Text) {
    $keep = @()
    foreach ($l in ($Text -split "[`r`n]+")) {
        $t = $l.TrimEnd()
        if ($t -eq '') { continue }
        if ($t -match '^\s*tx=') { continue }
        $keep += $t
    }
    return $keep
}

function Get-ResultField([string]$Text, [string]$Name) {
    $Text = $Text -replace "`r", "`n"
    $m = [regex]::Match($Text, '(?m)^RESULT .*\b' + [regex]::Escape($Name) + '=("([^"]*)"|(\S*))')
    if (-not $m.Success) { return '' }
    if ($m.Groups[2].Success) { return $m.Groups[2].Value }
    return $m.Groups[3].Value
}

# ---- locate the programs ---------------------------------------------------------------------------
$here = $PSScriptRoot
$parent = Split-Path -Parent $here
if (-not $ProbePath) {
    $ProbePath = Find-FirstFile @((Join-Path $parent 'dofscroll-hidprobe.exe'), (Join-Path $here 'dofscroll-hidprobe.exe'),
        (Join-Path (Join-Path $parent 'build\Release') 'dofscroll-hidprobe.exe'))
    if (-not $ProbePath) {
        $cmd = Get-Command 'dofscroll-hidprobe.exe' -ErrorAction SilentlyContinue
        if ($cmd) { $ProbePath = $cmd.Source }
    }
}
if (-not $ProbePath -or -not (Test-Path -LiteralPath $ProbePath -PathType Leaf)) {
    Write-Host 'dofscroll-hidprobe.exe not found. Run this script from the installed dofscroll folder (scripts\), or pass -ProbePath.'
    exit 2
}
if (-not $DaemonPath) {
    $DaemonPath = Find-FirstFile @((Join-Path (Split-Path -Parent $ProbePath) 'dofscroll.exe'), (Join-Path $parent 'dofscroll.exe'))
}
if (-not $ReportPath) { $ReportPath = Join-Path (Get-Location).ProviderPath 'dofscroll-driver-test-report.txt' }
$devicesDir = Join-Path (Split-Path -Parent $ProbePath) 'devices'

Write-Host 'dofscroll driver coexistence test'
Write-Host 'dofscroll is meant to run next to the official 3Dconnexion driver. Whether both can read the'
Write-Host 'SpaceMouse at the same time is not verified yet on real machines: this test finds out.'
Write-Host 'Nothing is changed: the script never stops or configures the driver. About 5 minutes.'
Write-Host ''

Add-Line 'dofscroll driver coexistence test report (Windows)'
Add-Line ('date (UTC): ' + [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm'))
Add-Line 'Questions for the maintainer: see docs/troubleshooting.md, "The official 3Dconnexion driver and dofscroll".'

# ---- (a) environment ---------------------------------------------------------------------------------
Add-Section 'System'
try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    Add-Line ('OS: ' + $os.Caption + ' ' + $os.Version + ' (build ' + $os.BuildNumber + '), ' + $os.OSArchitecture)
} catch {
    Add-Line ('OS: ' + [System.Environment]::OSVersion.VersionString)
}
Add-Line ('PowerShell: ' + $PSVersionTable.PSVersion.ToString())
$ver = Invoke-Capture $ProbePath @('--version') 10
Add-Line ('hidprobe: ' + (($ver.Output -split "[`r`n]+")[0]))
if ($DaemonPath) {
    $dv = Invoke-Capture $DaemonPath @('--version') 10
    Add-Line ('dofscroll: ' + (($dv.Output -split "[`r`n]+")[0]))
} else {
    Add-Line 'dofscroll: dofscroll.exe not found next to hidprobe (Test B will be skipped)'
}

Add-Section 'SpaceMouse devices (dofscroll-hidprobe --list)'
$listArgs = @('--list', '--devices-dir', $devicesDir)
$list = Invoke-Capture $ProbePath $listArgs 30
foreach ($l in (Get-ProbeLines $list.Output)) { Add-Line $l }
$deviceFound = ($list.Output -match '\[matches "')
if (-not $deviceFound) {
    Add-Line 'No SpaceMouse with a devices/*.conf description found.'
    Write-Host 'No known SpaceMouse found. Plug it in (receiver or cable) and run the test again.'
}

Add-Section 'Official 3Dconnexion driver'
$driverNames = @('3DxService', '3DxWinCore', '3DxWinCore64', '3dxpiemenus', '3DxMouseTray', '3DconnexionHelper')
$driverRunning = @()
foreach ($n in $driverNames) {
    $p = @(Get-Process -Name $n -ErrorAction SilentlyContinue)
    if ($p.Count -gt 0) { $driverRunning += $n }
}
if ($driverRunning.Count -gt 0) {
    Add-Line ('driver processes running: ' + ($driverRunning -join ', '))
} else {
    Add-Line 'driver processes running: none of 3DxService, 3DxWinCore, 3dxpiemenus found'
}
$svc = Get-Service -Name '3Dconnexion*', '3DxService*' -ErrorAction SilentlyContinue
if ($svc) { foreach ($s in $svc) { Add-Line ('service: ' + $s.Name + ' = ' + $s.Status) } }
$driverVersion = ''
foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $pf) { continue }
    foreach ($sub in @('3Dconnexion\3DxWare\3DxWinCore\3DxService.exe', '3Dconnexion\3DxWare\3DxWinCore64\3DxService.exe', '3Dconnexion\3DxWare\3DxService.exe')) {
        $f = Join-Path $pf $sub
        if (Test-Path -LiteralPath $f -PathType Leaf) {
            try { $driverVersion = (Get-Item -LiteralPath $f).VersionInfo.FileVersion } catch { }
            if ($driverVersion) { break }
        }
    }
    if ($driverVersion) { break }
}
if ($driverVersion) { Add-Line ('driver version (3DxService.exe): ' + $driverVersion) } else { Add-Line 'driver version: not found (no 3DxService.exe under Program Files\3Dconnexion\3DxWare)' }
$driverActive = ($driverRunning.Count -gt 0)
Add-Verdict ('Driver running: ' + $(if ($driverActive) { 'YES' } else { 'NO' }))

# ---- tests with hidprobe ---------------------------------------------------------------------------------
function Test-Reading([string]$Label) {
    $r = @{ Opened = 'no'; Reports = 0; Axes = ''; Error = ''; Exit = -1 }
    Write-Host ''
    if (-not $NonInteractive) {
        Write-Host ('Test ' + $Label + ': when you press Enter, move the SpaceMouse cap for ' + $Seconds + ' seconds:')
        Write-Host '  push it in all directions, tilt it, twist it, and press its buttons.'
        Wait-Enter 'Press Enter to start'
    }
    $secs = if ($NonInteractive) { 2 } else { $Seconds }
    $cap = Invoke-Capture $ProbePath @('--check', '--seconds', [string]$secs, '--devices-dir', $devicesDir) ($secs + 40)
    $r.Exit = $cap.ExitCode
    foreach ($l in (Get-ProbeLines $cap.Output)) { Add-Line $l }
    if ($cap.TimedOut) { Add-Line 'hidprobe did not finish in time and was ended by the script' }
    Add-Line ('hidprobe exit code: ' + $cap.ExitCode + ' (0 = reports received, 3 = opened but no report, 1 = could not open, 2 = usage)')
    $r.Opened = Get-ResultField $cap.Output 'opened'
    if (-not $r.Opened) { $r.Opened = 'no' }
    $err = Get-ResultField $cap.Output 'error'
    if ($err) { $r.Error = $err }
    $n = Get-ResultField $cap.Output 'reports'
    if ($n -match '^\d+$') { $r.Reports = [int]$n }
    $r.Axes = Get-ResultField $cap.Output 'axes_moved'
    if (-not $deviceFound) { $r.Opened = 'no'; $r.Error = 'no device found' }
    return $r
}

function Write-ReadingVerdicts([string]$Letter, [string]$State, $r) {
    Add-Verdict ($Letter + ': device opened while ' + $State + ': ' + $(if ($r.Opened -eq 'yes') { 'YES' } else { 'NO' }))
    if ($r.Error) { Add-Line ($Letter + ': error text: ' + $r.Error) }
    Add-Verdict ($Letter + ': reports received while ' + $State + ': ' + $(if ($r.Reports -gt 0) { 'YES (' + $r.Reports + ' reports)' } else { 'NO' }))
    Add-Line ($Letter + ': axes seen: ' + $(if ($r.Axes) { $r.Axes } else { 'none' }))
}

Add-Section 'TEST A: driver running, hidprobe reads the device'
$resA = Test-Reading 'A'
Write-ReadingVerdicts 'A' 'the driver runs' $resA
if ($driverActive -and $resA.Opened -eq 'yes' -and $resA.Reports -eq 0 -and -not $NonInteractive) {
    Add-Line 'A: note: the device opened but nothing arrived. Either the cap was not moved, or the driver keeps the reports for itself.'
}

# ---- Test B ------------------------------------------------------------------------------------------------
Add-Section 'TEST B: driver and dofscroll together'
$started = $null
$logPath = ''
$statusPath = ''
$logStart = 0
$rtDir = Join-Path $env:LOCALAPPDATA 'dofscroll'
$logPath = Join-Path $rtDir 'dofscroll.log'
$statusPath = Join-Path $rtDir 'dofscroll-status'
$bDone = $false
$bDoubled = 'skipped'
$bCad = 'skipped'
$bReacts = 'skipped'
if (-not $DaemonPath) {
    Add-Line 'B: skipped (dofscroll.exe not found)'
} elseif (-not $deviceFound) {
    Add-Line 'B: skipped (no SpaceMouse found)'
} elseif ($NonInteractive) {
    Add-Line 'B: skipped (non-interactive)'
} else {
    $running = @(Get-Process -Name 'dofscroll' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Add-Line 'B: a dofscroll daemon was already running; the script uses it and does not start or stop one'
    } else {
        if (Test-Path -LiteralPath $logPath) { try { $logStart = @(Get-Content -LiteralPath $logPath -ErrorAction Stop).Count } catch { $logStart = 0 } }
        try {
            $started = Start-Process -FilePath $DaemonPath -PassThru -WindowStyle Hidden
            Add-Line 'B: the script started dofscroll for this test (it ends it again afterwards)'
        } catch {
            Add-Line ('B: could not start dofscroll: ' + $_.Exception.Message)
        }
        Start-Sleep -Seconds 4
    }
    Write-Host ''
    Write-Host 'dofscroll and the 3Dconnexion driver are now running together. Please try:'
    Write-Host '  1. Open a web page or a long document in a browser, put the mouse pointer over it'
    Write-Host '     and push/tilt the SpaceMouse cap for about 10 seconds.'
    Write-Host '  2. Then do the same in your CAD program (rotate, pan and zoom a model).'
    Wait-Enter 'Press Enter when you have tried both'
    $bDoubled = Read-YesNo 'In the browser, did it scroll TWICE as much, or jump or stutter (as if two programs scrolled)?'
    $bCad = Read-YesNo 'Did the CAD program still react normally to the SpaceMouse (rotate, pan, zoom)?'
    $bReacts = Read-YesNo 'Did dofscroll scroll or zoom in the browser (as your dofscroll settings say)?'
    $bDone = $true
}
if ($bDone) {
    Add-Line ''
    Add-Line 'dofscroll log (new lines only, lines about the device and errors):'
    $shown = 0
    if (Test-Path -LiteralPath $logPath) {
        try {
            $all = @(Get-Content -LiteralPath $logPath -ErrorAction Stop)
            $new = if ($all.Count -gt $logStart) { $all[$logStart..($all.Count - 1)] } else { @() }
            foreach ($l in $new) {
                if ($l -match 'device|Device|HID|cannot|error|Error|connected|SpaceMouse|driver|exclusive|attached|removed|input') {
                    Add-Line ('  ' + $l)
                    $shown++
                    if ($shown -ge 40) { break }
                }
            }
        } catch { Add-Line ('  (log not readable: ' + $_.Exception.Message + ')') }
    }
    if ($shown -eq 0) { Add-Line '  (no matching lines; log file: %LOCALAPPDATA%\dofscroll\dofscroll.log)' }
    Add-Line 'dofscroll status file:'
    if (Test-Path -LiteralPath $statusPath) {
        try {
            foreach ($l in @(Get-Content -LiteralPath $statusPath -ErrorAction Stop)) {
                if ($l -match '^(device|devices_seen|input_issue|enabled|paused|updated)=') { Add-Line ('  ' + $l) }
            }
        } catch { Add-Line '  (not readable)' }
    } else { Add-Line '  (no status file)' }
    if ($started -ne $null) {
        # the only thing the script ends: the dofscroll it started itself, through the daemon's own quit event
        $stop = Join-Path $here 'stop-daemon.ps1'
        if (Test-Path -LiteralPath $stop -PathType Leaf) {
            try { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $stop -ExePath $DaemonPath | Out-Null } catch { }
        } else {
            try { $started.CloseMainWindow() | Out-Null } catch { }
        }
        Add-Line 'B: the dofscroll started by the script was asked to quit'
    }
    Add-Verdict ('B: scrolling doubled: ' + $(if ($bDoubled -eq 'yes') { 'YES' } elseif ($bDoubled -eq 'no') { 'NO' } else { 'NOT ANSWERED' }))
    Add-Verdict ('B: CAD program still reacts normally: ' + $(if ($bCad -eq 'yes') { 'YES' } elseif ($bCad -eq 'no') { 'NO' } else { 'NOT ANSWERED' }))
    Add-Verdict ('B: dofscroll reacts while the driver runs: ' + $(if ($bReacts -eq 'yes') { 'YES' } elseif ($bReacts -eq 'no') { 'NO' } else { 'NOT ANSWERED' }))
}

# ---- Test C ------------------------------------------------------------------------------------------------
Add-Section 'TEST C (optional, for diagnosis only): driver stopped'
$resC = $null
$cAsked = 'skipped'
if ($NonInteractive -or -not $driverActive) {
    Add-Line 'C: skipped'
} else {
    Write-Host ''
    Write-Host 'Test C is for diagnosis only. Normal users do not need to do it: dofscroll is meant to run'
    Write-Host 'next to the driver, and this script never stops the driver. If the maintainer asked you to,'
    Write-Host 'you can end the driver yourself (for example exit 3Dconnexion from the tray icon, or stop'
    Write-Host 'the 3Dconnexion service in Services) and start it again afterwards.'
    $cAsked = Read-YesNo 'Do you want to do Test C now (you stop the driver yourself, then answer yes)?'
    if ($cAsked -eq 'yes') {
        Write-Host 'Stop the driver now, then come back here.'
        Wait-Enter 'Press Enter when the driver is stopped'
        $still = @()
        foreach ($n in $driverNames) { if (@(Get-Process -Name $n -ErrorAction SilentlyContinue).Count -gt 0) { $still += $n } }
        Add-Line ('C: driver processes still running: ' + $(if ($still.Count -gt 0) { $still -join ', ' } else { 'none' }))
        $resC = Test-Reading 'C'
        Write-ReadingVerdicts 'C' 'the driver is stopped' $resC
        Write-Host 'You can start the driver again now.'
    } else {
        Add-Line 'C: skipped'
    }
}

# ---- summary -------------------------------------------------------------------------------------------------
Add-Section 'Summary'
foreach ($v in $script:Verdicts) { Add-Line $v }
if ($resC -ne $null -and $resA.Reports -eq 0 -and $resC.Reports -gt 0) {
    Add-Line 'Reading: no reports while the driver runs, reports with the driver stopped: the driver keeps the device for itself (this is a bug of dofscroll to be fixed).'
}

# ---- write the report (no user name, no personal paths) -----------------------------------------------------
function Remove-Personal([string]$Text) {
    $t = $Text
    foreach ($h in @($env:USERPROFILE, ($env:USERPROFILE -replace '\\', '/'))) {
        if ($h -and $h.Length -gt 3) { $t = $t.Replace($h, '~') }
    }
    foreach ($base in @($env:APPDATA, $env:LOCALAPPDATA, $env:TEMP)) {
        if ($base -and $base.Length -gt 3) { $t = $t.Replace($base, '<appdata>') }
    }
    if ($env:USERNAME -and $env:USERNAME.Length -gt 1) { $t = [regex]::Replace($t, [regex]::Escape($env:USERNAME), '<user>', 'IgnoreCase') }
    if ($env:COMPUTERNAME -and $env:COMPUTERNAME.Length -gt 1) { $t = [regex]::Replace($t, [regex]::Escape($env:COMPUTERNAME), '<host>', 'IgnoreCase') }
    return $t
}
$text = Remove-Personal (($script:Lines -join "`r`n") + "`r`n")
try {
    [System.IO.File]::WriteAllText($ReportPath, $text, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host ('Report written to: ' + (Remove-Personal $ReportPath))
    Write-Host 'Please read it once (it has no user name and no personal paths), then attach this file to'
    Write-Host 'issue #36 (Windows) of the dofscroll repository, or paste its text into a comment.'
} catch {
    Write-Host ('Could not write the report: ' + $_.Exception.Message)
    Write-Host $text
    exit 1
}
exit 0
