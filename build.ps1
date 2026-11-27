# Build PaintRebuild (native Win32). Locates the C++ toolchain itself.
param(
    [switch]$Clean,
    [switch]$Verbose,
    [string[]]$Only = @()   # selftest, app (app appears in Stage 6)
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'

function Find-VcVars {
    $candidates = @(
        "$env:PR_VCVARS",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($install) {
            $p = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $p) { return $p }
        }
    }
    return $null
}

function Find-MingwGxx {
    $candidates = @(
        "$env:PR_MINGW_GXX",
        'G:\PROJECTS\.toolchain\winlibs-mingw64\bin\g++.exe',
        'G:\PROJECTS\.toolchain\mingw64\bin\g++.exe',
        "$env:USERPROFILE\.toolchain\winlibs-mingw64\bin\g++.exe",
        'C:\mingw64\bin\g++.exe',
        'C:\msys64\mingw64\bin\g++.exe'
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    $inPath = Get-Command g++ -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    return $null
}

$vcvars = Find-VcVars
$mingw = $null
if (-not $vcvars) { $mingw = Find-MingwGxx }
if (-not $vcvars -and -not $mingw) {
    throw 'No C++ toolchain found. Install VS Build Tools (C++ workload) or extract portable MinGW-w64 and set PR_MINGW_GXX to its g++.exe.'
}
if ($Verbose) { Write-Host "toolchain: $(if ($vcvars) { $vcvars } else { $mingw })" }

if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
New-Item -ItemType Directory -Force -Path $build | Out-Null

$coreSrc = @(Get-ChildItem -Path (Join-Path $root 'src\core') -Filter '*.cpp' -ErrorAction SilentlyContinue |
             ForEach-Object { '..\src\core\' + $_.Name })
$toolSrc = @(Get-ChildItem -Path (Join-Path $root 'src\tools') -Filter '*.cpp' -ErrorAction SilentlyContinue |
             ForEach-Object { '..\src\tools\' + $_.Name })
$testSrc = @('..\src\tests\selftest.cpp')
$want = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

function Build-Step([string]$key, [string]$name, [string]$cmd) {
    if ($want.Count -gt 0 -and $want -notcontains $key) { return }
    Write-Host "building $name ..."
    if ($vcvars) { cmd /c $cmd } else { Invoke-Expression $cmd }
    if ($LASTEXITCODE -ne 0) { throw "$name build failed ($LASTEXITCODE)" }
}

Push-Location $build
try {
    if ($vcvars) {
        $common = '/nologo /EHsc /std:c++17 /W3 /O2 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS'
        $src = ($testSrc + $coreSrc + $toolSrc) -join ' '
        Build-Step 'selftest' 'pr_selftest.exe' "call `"$vcvars`" >nul && cl $common $src /Fe:pr_selftest.exe /link /SUBSYSTEM:CONSOLE"
    } else {
        $common = '-std=c++17 -O2 -Wall -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0602 -DWINVER=0x0602 -static-libgcc -static-libstdc++'
        $src = ($testSrc + $coreSrc + $toolSrc) -join ' '
        Build-Step 'selftest' 'pr_selftest.exe' "& `"$mingw`" $common $src -o pr_selftest.exe"
    }
} finally {
    Pop-Location
}

Write-Host "build OK -> $build"
