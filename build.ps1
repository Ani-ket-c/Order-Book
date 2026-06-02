# Windows build script (Visual Studio 2022 + CMake)
# Usage: .\build.ps1          # Release build
#        .\build.ps1 -Test     # also run lob_gateway_test
#        .\build.ps1 -Run      # run main benchmark executable

param(
    [switch]$Test,
    [switch]$Run,
    [switch]$Clean,
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [int]$Steps = 10000
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build"

# Locate CMake (PATH or default install path)
$Cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $Cmake) {
    $CmakePath = "C:\Program Files\CMake\bin\cmake.exe"
    if (-not (Test-Path $CmakePath)) {
        Write-Error "CMake not found. Install: winget install Kitware.CMake"
    }
    $Cmake = $CmakePath
} else {
    $Cmake = $Cmake.Source
}

# Visual Studio 2022 environment
$VcVars = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $VcVars)) {
    $VcVars = "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $VcVars)) {
    Write-Error "Visual Studio 2022 (C++ workload) not found. Install 'Desktop development with C++'."
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$configureCmd = 'call "{0}" >nul & "{1}" -S "{2}" -B "{3}" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE={4}' -f $VcVars, $Cmake, $Root, $BuildDir, $Config
cmd /c $configureCmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$cleanFlag = if ($Clean) { "--clean-first" } else { "" }
$buildCmd = 'call "{0}" >nul & "{1}" --build "{2}" --config {3} --parallel {4}' -f $VcVars, $Cmake, $BuildDir, $Config, $cleanFlag
cmd /c $buildCmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

function Get-BuildOutputDir {
    param([string]$Dir, [string]$Cfg)
    $candidates = @(
        (Join-Path $Dir $Cfg),
        (Join-Path $Dir "bin\$Cfg"),
        (Join-Path $Dir "x64\$Cfg")
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "lob_gateway_test.exe")) { return $c }
        if (Test-Path (Join-Path $c "LimitOrderBook.exe")) { return $c }
    }
    return (Join-Path $Dir $Cfg)
}

$Bin = Get-BuildOutputDir -Dir $BuildDir -Cfg $Config
Write-Host "`nBuild OK -> $Bin" -ForegroundColor Green

if ($Test) {
    $exe = Join-Path $Bin "lob_gateway_test.exe"
    if (-not (Test-Path $exe)) { Write-Error "Not found: $exe" }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Run) {
    $exe = Join-Path $Bin "LimitOrderBook.exe"
    if (-not (Test-Path $exe)) { Write-Error "Not found: $exe" }
    & $exe $Steps
}
