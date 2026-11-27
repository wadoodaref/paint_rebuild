# Locate the installed Microsoft Paint artifacts on THIS machine (rules v7 section 8).
# Verifies exact files rather than trusting a recorded path. Read-only.
param([switch]$Json)

$ErrorActionPreference = 'Stop'
$results = @()

$packages = Get-AppxPackage -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in @('Microsoft.Paint', 'Microsoft.MSPaint') }
foreach ($package in $packages) {
    foreach ($relative in @('PaintApp\mspaint.exe', 'PaintStudio.View.exe')) {
        $exe = Join-Path $package.InstallLocation $relative
        if (Test-Path $exe) {
            $results += [pscustomobject]@{
                package = $package.PackageFullName
                exe     = $exe
                sha256  = (Get-FileHash -Path $exe -Algorithm SHA256).Hash.ToLowerInvariant()
                source  = 'AppxPackage'
            }
        }
    }
}

# Legacy paths are hints only; each is verified before being reported.
foreach ($hint in @("$env:WINDIR\System32\mspaint.exe", "$env:WINDIR\SysWOW64\mspaint.exe")) {
    if (Test-Path $hint) {
        $results += [pscustomobject]@{
            package = '(legacy system path)'
            exe     = $hint
            sha256  = (Get-FileHash -Path $hint -Algorithm SHA256).Hash.ToLowerInvariant()
            source  = 'LegacyHint'
        }
    }
}

if ($Json) {
    $results | ConvertTo-Json -Depth 4
} else {
    $results | Format-Table -AutoSize
    Write-Host ''
    Write-Host 'REA/MCP follow-ups (read-only, no target execution):'
    Write-Host '  tools.rea.inspect_pe_imports({ path: <exe> })'
    Write-Host '  tools.rea.inspect_native_exports({ path: <module dll>, detail: "summary" })'
    Write-Host '  tools.rea.inspect_managed_artifact({ path: <winmd>, detail: "summary" })'
}

if ($results.Count -eq 0) {
    Write-Warning 'No Paint artifact found. Search this machine drives (bounded) or ask the user - do not guess a path.'
}
