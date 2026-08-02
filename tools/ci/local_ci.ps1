<#
.SYNOPSIS
  Local CI: configure + build + ctest for the Windows preset (M0-T6).

.DESCRIPTION
  The same sequence the GitHub workflow runs, so a green run here means a green
  run there. Worth running before pushing: "CI red on main => no new task
  claims" (11 §3), and finding out locally costs minutes rather than a round
  trip through Actions.

  Exits non-zero on the first failure so callers can rely on the status.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ci/local_ci.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/ci/local_ci.ps1 -Config Debug -Clean
#>
[CmdletBinding()]
param(
    [string]$Preset = 'win-x64',
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Both   # run Debug and Release, as the workflow does
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

$configs = if ($Both) { @('Debug', 'Release') } else { @($Config) }

if ($Clean) {
    $buildDir = Join-Path $root "build/$Preset"
    if (Test-Path $buildDir) {
        Write-Host "[local_ci] removing $buildDir"
        Remove-Item -Recurse -Force $buildDir
    }
}

Write-Host "=== local_ci: configure ($Preset) ===" -ForegroundColor Cyan
& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { Write-Error "configure failed (exit $LASTEXITCODE)" }

$failed = @()
foreach ($c in $configs) {
    $suffix = if ($c -eq 'Debug') { 'deb' } else { 'rel' }
    $buildPreset = "$Preset-$suffix"

    Write-Host "=== local_ci: $buildPreset ===" -ForegroundColor Cyan
    & cmake --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { $failed += "build $buildPreset"; continue }

    & ctest --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { $failed += "ctest $buildPreset"; continue }

    Write-Host "--- local_ci: $buildPreset OK" -ForegroundColor Green
}

if ($failed.Count -gt 0) {
    Write-Host '[local_ci] FAILED' -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
    exit 1
}
Write-Host '[local_ci] OK' -ForegroundColor Green
exit 0
