# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
<#
.SYNOPSIS
  Stages dofscroll for Windows and builds the per-user NSIS installer and the portable ZIP.

.DESCRIPTION
  Input: a Release build with the daemon and the interface (build\Release\dofscroll.exe and
  dofscroll-ui.exe; both are required, there is no daemon-only package). windeployqt copies the Qt runtime
  next to the interface. Output in -OutDir:
    dofscroll-<version>-windows-x64-setup.exe     (installer, needs no administrator rights)
    dofscroll-<version>-windows-x64-portable.zip
  Needs makensis (NSIS 3) in PATH.

.PARAMETER BuildDir   CMake build directory (default: build)
.PARAMETER OutDir     Output directory (default: dist-installers)
.PARAMETER QtBin      Qt bin directory with windeployqt.exe (default: $env:QT_ROOT_DIR\bin)
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$OutDir = 'dist-installers',
    [string]$QtBin = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath

$cm = Get-Content -Raw (Join-Path $root 'CMakeLists.txt')
if ($cm -notmatch 'project\(dofscroll VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw 'project version not found in CMakeLists.txt' }
$version = $Matches[1]

$rel = Join-Path $BuildDir 'Release'
$daemon = Join-Path $rel 'dofscroll.exe'
$ui = Join-Path $rel 'dofscroll-ui.exe'
if (-not (Test-Path -LiteralPath $daemon)) { throw "not built: $daemon" }
if (-not (Test-Path -LiteralPath $ui)) { throw "not built: $ui (every package contains the interface and the daemon; build with -DDOFSCROLL_UI=ON)" }
# The diagnostic tool ships with every package (docs/devices.md, scripts\test-driver-coexistence.ps1).
$probe = Join-Path $rel 'dofscroll-hidprobe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw "not built: $probe" }
$base = "dofscroll-$version-windows-x64"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).ProviderPath
$stage = Join-Path $OutDir 'stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $daemon $stage
Copy-Item $probe $stage
Copy-Item -Recurse (Join-Path $root 'devices') (Join-Path $stage 'devices')
Copy-Item -Recurse (Join-Path $root 'examples') (Join-Path $stage 'examples')
Copy-Item -Recurse (Join-Path $root 'icons') (Join-Path $stage 'icons')
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD-PARTY-NOTICES.md') $stage
$scripts = Join-Path $stage 'scripts'
New-Item -ItemType Directory -Force -Path $scripts | Out-Null
Copy-Item (Join-Path $root 'packaging\windows\*.ps1') $scripts -Exclude 'build-installer.ps1'
Copy-Item (Join-Path $root 'scripts\test-driver-coexistence.ps1') $scripts

Copy-Item $ui $stage
Get-ChildItem -Path $rel -Filter '*.qm' | Copy-Item -Destination $stage
if (-not $QtBin) { $QtBin = Join-Path $env:QT_ROOT_DIR 'bin' }
$wdq = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $wdq)) { throw "windeployqt not found: $wdq" }
& $wdq --release --no-compiler-runtime --no-translations (Join-Path $stage 'dofscroll-ui.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# App-local Microsoft C++ runtime (the daemon and Qt are built with /MD): copy the redistributable
# DLLs of the build toolchain so the program runs on a machine without the VC++ redistributable.
$crt = Get-ChildItem -Directory -ErrorAction SilentlyContinue `
    "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" |
    Sort-Object FullName | Select-Object -Last 1
if ($crt) {
    Get-ChildItem $crt.FullName -Filter '*.dll' | Copy-Item -Destination $stage
    Write-Output "runtime: copied $($crt.Name)"
} else {
    Write-Warning 'VC++ runtime DLLs not found: the package needs the VC++ redistributable on the target'
}

@"
dofscroll $version for Windows (portable, unsigned)

Unpack this folder anywhere and start dofscroll.exe (daemon, no window) and dofscroll-ui.exe
(settings window and tray icon, when present). Windows SmartScreen may warn because the program
is not code-signed: More info > Run anyway. Configuration: %APPDATA%\dofscroll\dofscroll.conf.
Autostart for the current user: scripts\install-autostart.ps1 (remove: scripts\uninstall-autostart.ps1).
Diagnostics: dofscroll-hidprobe.exe (run it from a terminal: dofscroll-hidprobe --list) and the guided
test "can dofscroll run next to the official 3Dconnexion driver?":
    powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1
It takes about 5 minutes and writes dofscroll-driver-test-report.txt (see docs/devices.md).
Licenses: LICENSE, THIRD-PARTY-NOTICES.md.
"@ | Set-Content -Encoding utf8 (Join-Path $stage 'README-portable.txt')

# Optional icon: only when ImageMagick can render the SVG (the installer works without it).
$icoArg = @()
$magick = Get-Command magick -ErrorAction SilentlyContinue
if ($magick) {
    $ico = Join-Path $OutDir 'dofscroll.ico'
    try {
        & $magick.Source -background none -density 384 (Join-Path $root 'icons\dofscroll-app.svg') -define icon:auto-resize=256,64,48,32,16 $ico
        if ($LASTEXITCODE -eq 0 -and (Test-Path $ico)) { $icoArg = @("/DAPP_ICO=$ico") }
    } catch { Write-Warning "icon conversion failed: $_" }
}

$setup = Join-Path $OutDir "$base-setup.exe"
$nsi = Join-Path $root 'packaging\windows\installer.nsi'
$nsisArgs = @('/V2', "/DVERSION=$version", "/DSTAGE=$stage", "/DOUTFILE=$setup")
$nsisArgs += '/DHAS_UI'
$nsisArgs += $icoArg
$nsisArgs += $nsi
Write-Output ("makensis " + ($nsisArgs -join ' '))
& makensis $nsisArgs
if ($LASTEXITCODE -ne 0) { throw "makensis failed ($LASTEXITCODE)" }

$zip = Join-Path $OutDir "$base-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
$top = Join-Path $OutDir 'zip'
if (Test-Path $top) { Remove-Item -Recurse -Force $top }
New-Item -ItemType Directory -Path $top | Out-Null
Copy-Item -Recurse $stage (Join-Path $top "dofscroll-$version")
Compress-Archive -Path (Join-Path $top "dofscroll-$version") -DestinationPath $zip
Remove-Item -Recurse -Force $top

Write-Output "setup:  $setup"
Write-Output "zip:    $zip"
