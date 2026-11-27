# Canonical start point: build, run the headless selftest, launch the app (Stage 6+).
param([switch]$NoBuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'build\pr_selftest.exe'

if (-not $NoBuild) {
    & (Join-Path $root 'build.ps1')
    if (-not $?) { throw 'build.ps1 failed' }
}
if (-not (Test-Path $exe)) { throw "missing $exe" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "selftest failed ($LASTEXITCODE)" }

$app = Join-Path $root 'build\paintrebuild.exe'
if (Test-Path $app) {
    Write-Host "launching PaintRebuild ..."
    Start-Process -FilePath $app
} else {
    Write-Host 'app target arrives in Stage 6; headless selftest completed.'
}
