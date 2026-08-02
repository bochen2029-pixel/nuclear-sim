<#
.SYNOPSIS
  Configure, build and test nuclear-sim. Wraps the canonical loop in
  spec/12-deployment.md §2.

.DESCRIPTION
  The canonical loop is three plain cmake commands and stays runnable by hand;
  this script exists for the two things the bare commands cannot express:

    -Clean      delete the preset's build directory first
    -ColdCache  additionally point vcpkg's binary cache at a throwaway directory,
                so the ports are genuinely rebuilt from source

  -ColdCache is how M0-T2's "CLEAN build dir + EMPTY vcpkg cache" evidence is
  produced. It deliberately does NOT delete %LOCALAPPDATA%\vcpkg\archives: that
  cache is shared with every other project on the machine, and redirecting it
  proves the same thing — that this build depends on no cached state.

  Exits nonzero on any failure so tools/ci (M0-T6) can rely on it.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Clean -ColdCache
#>
[CmdletBinding()]
param(
    [string]$Preset = 'win-x64',
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$ColdCache,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Build preset names are normative in 12 §1: <configure>-deb / <configure>-rel.
$suffix = if ($Config -eq 'Debug') { 'deb' } else { 'rel' }
$buildPreset = "$Preset-$suffix"
$buildDir = Join-Path $root "build/$Preset"

function Invoke-Step {
    param([string]$Label, [string[]]$Arguments)
    Write-Host "[build] $Label" -ForegroundColor Cyan
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed (exit $LASTEXITCODE)" }
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[build] removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

if ($ColdCache) {
    $scratch = Join-Path $root "build/_coldcache-$Preset"
    if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    $env:VCPKG_DEFAULT_BINARY_CACHE = $scratch
    Write-Host "[build] cold vcpkg cache: $scratch" -ForegroundColor Yellow
}

Invoke-Step "configure ($Preset)" @('--preset', $Preset)
Invoke-Step "build ($buildPreset)" @('--build', '--preset', $buildPreset)

if (-not $SkipTests) {
    Write-Host "[build] ctest ($buildPreset)" -ForegroundColor Cyan
    & ctest --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { throw "ctest failed (exit $LASTEXITCODE)" }
}

Write-Host "[build] OK ($Preset / $Config)" -ForegroundColor Green
exit 0
