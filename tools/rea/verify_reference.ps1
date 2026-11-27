# Verify the vendored REA reference bundle against manifest.json.
# Runs offline on any PC; no Paint install needed.
param([string]$Bundle = (Join-Path $PSScriptRoot '..\..\reference\paint-11.2609.441.0'))

$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $Bundle 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw "missing $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$failures = 0
foreach ($entry in $manifest.files) {
    $file = Join-Path $Bundle $entry.file
    if (-not (Test-Path -LiteralPath $file)) {
        Write-Error "MISSING $($entry.file)"
        $failures++
        continue
    }
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $file).Length
    if ($actual -ne $entry.sha256 -or $size -ne $entry.bytes) {
        Write-Error "MISMATCH $($entry.file): sha=$actual bytes=$size"
        $failures++
    } else {
        Write-Host "OK  $($entry.file)"
    }
}
Write-Host "verify_reference: $($manifest.files.Count) files, $failures FAIL"
if ($failures -eq 0) { exit 0 } else { exit 1 }
