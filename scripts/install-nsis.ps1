# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Installs NSIS (makensis) on a Windows CI runner from a pinned, verified source instead of a
# package feed: downloads the official zip, checks a pinned SHA-256 and unpacks it into
# $RUNNER_TEMP. Each download is tried 3 times with a pause; the sources are tried in order:
# the optional mirror (-MirrorUrl, for example a release asset), then the official URL.
# The downloaded zip is kept in -CacheDir so that actions/cache can restore it.
# Used by installers.yml (and so by installers-dry-run.yml). See docs/release-process.md.
param(
  [string]$Version = '3.11',
  [string]$Sha256 = 'c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1',
  [string]$MirrorUrl = '',
  [string]$CacheDir = (Join-Path $env:RUNNER_TEMP 'nsis-cache'),
  [string]$InstallDir = (Join-Path $env:RUNNER_TEMP 'nsis'),
  [int]$Attempts = 3,
  [int]$PauseSeconds = 10
)
$ErrorActionPreference = 'Stop'

$name = "nsis-$Version.zip"
$zip = Join-Path $CacheDir $name
New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

function Test-Hash($path) {
  (Test-Path -LiteralPath $path) -and ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha256.ToLowerInvariant())
}

if (-not (Test-Hash $zip)) {
  Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
  $urls = @()
  if ($MirrorUrl) { $urls += $MirrorUrl }
  $urls += "https://prdownloads.sourceforge.net/nsis/$name"
  foreach ($url in $urls) {
    for ($i = 1; $i -le $Attempts; $i++) {
      try {
        Write-Output "download ($i/$Attempts): $url"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 120
        if (Test-Hash $zip) { break }
        $got = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
        throw "SHA-256 mismatch for ${url}: expected $Sha256, got $got"
      } catch {
        Write-Output "::warning::NSIS download failed: $url ($($_.Exception.Message))"
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        if ($i -lt $Attempts) { Start-Sleep -Seconds $PauseSeconds }
      }
    }
    if (Test-Path -LiteralPath $zip) { break }
  }
  if (-not (Test-Hash $zip)) { throw "NSIS $Version could not be downloaded and verified from: $($urls -join ', ')" }
}
Write-Output "NSIS zip verified (SHA-256 $Sha256)"

Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $zip -DestinationPath $InstallDir -Force
$bin = Join-Path $InstallDir "nsis-$Version"
if (-not (Test-Path -LiteralPath (Join-Path $bin 'makensis.exe'))) { throw "makensis.exe not found in $bin" }
if ($env:GITHUB_PATH) { $bin | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8 }
$env:PATH = "$bin;$env:PATH"
& makensis /VERSION
if ($LASTEXITCODE -ne 0) { throw 'makensis does not run' }
