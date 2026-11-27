# Print local paths and exact REA/MCP calls for the vendored Paint reference.
# No machine-dependent search: paths are relative to this repo.
param([switch]$Json)

$ErrorActionPreference = 'Stop'
$bundle = Join-Path $PSScriptRoot '..\..\reference\paint-11.2609.441.0'
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
$items = @()
foreach ($entry in $manifest.files) {
    $items += [pscustomobject]@{
        file  = $entry.file
        path  = (Join-Path $bundle $entry.file)
        sha256 = $entry.sha256
    }
}
if ($Json) {
    $items | ConvertTo-Json -Depth 3
} else {
    $items | Format-Table -AutoSize
    Write-Host ''
    Write-Host 'REA/MCP calls (read-only examples):'
    Write-Host '  tools.rea.inspect_pe_imports({ path: "<bundle>\mspaint.exe" })'
    Write-Host '  tools.rea.inspect_native_exports({ path: "<bundle>\PaintTools.dll", detail: "summary" })'
    Write-Host '  tools.rea.inspect_native_exports({ path: "<bundle>\BasePaint.dll", detail: "summary" })'
    Write-Host '  tools.rea.inspect_managed_artifact({ path: "<bundle>\PaintUI.winmd", detail: "summary" })'
    Write-Host ''
    Write-Host 'Run verify_reference.ps1 first to confirm the bundle is intact.'
}
