# Full rippled build with Build Governor protection
# This script handles conan install, cmake configure, and ninja build
#
# Usage:
#   .\build-rippled.ps1                     # Build with defaults (Release, -j8)
#   .\build-rippled.ps1 -Parallel 4         # Use 4 parallel jobs
#   .\build-rippled.ps1 -BuildType Debug    # Debug build
#   .\build-rippled.ps1 -Clean              # Clean and rebuild
#   .\build-rippled.ps1 -ToolkitPath C:\... # Specify toolkit location

param(
    [int]$Parallel = 8,
    [string]$BuildType = "Release",
    [switch]$Clean,
    [string]$ToolkitPath = ""
)

$ErrorActionPreference = "Stop"

# Auto-detect toolkit path if not specified
if (-not $ToolkitPath) {
    # Check common locations
    $possiblePaths = @(
        "$PSScriptRoot\..",
        "F:\AI\rippled-windows-debug",
        "$env:USERPROFILE\rippled-windows-debug",
        ".\rippled-windows-debug"
    )
    foreach ($path in $possiblePaths) {
        if (Test-Path "$path\tools\build-governor") {
            $ToolkitPath = (Resolve-Path $path).Path
            break
        }
    }
}

if (-not $ToolkitPath -or -not (Test-Path "$ToolkitPath\tools\build-governor")) {
    Write-Host "ERROR: Could not find rippled-windows-debug toolkit!" -ForegroundColor Red
    Write-Host "Either run this script from the toolkit, or specify -ToolkitPath" -ForegroundColor Red
    exit 1
}

# Use current directory as rippled source
$rippledDir = Get-Location
if (-not (Test-Path "conanfile.py") -or -not (Test-Path "CMakeLists.txt")) {
    Write-Host "ERROR: This doesn't look like a rippled directory!" -ForegroundColor Red
    Write-Host "Run this script from the rippled source root." -ForegroundColor Red
    exit 1
}

# Check for required dependencies
Write-Host "Checking dependencies..." -ForegroundColor Gray
$missing = @()

# Check for .NET SDK
$dotnetVersion = & dotnet --version 2>$null
if (-not $dotnetVersion) {
    $missing += ".NET SDK (required for Build Governor)"
}

# Check for Python/Conan
$conanPath = "$env:APPDATA\Python\Python314\Scripts\conan.exe"
if (-not (Test-Path $conanPath)) {
    $conanPath = (Get-Command conan -ErrorAction SilentlyContinue).Source
}
if (-not $conanPath) {
    $missing += "Conan 2.x (pip install conan)"
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "ERROR: Missing dependencies:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Please install the missing dependencies and try again." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  rippled Build with Build Governor Protection" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Build Type:  $BuildType"
Write-Host "  Parallelism: $Parallel"
Write-Host ""

# Build directory
$buildDir = "build-gov"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# Set up VS2022 environment
Write-Host "Setting up Visual Studio 2022 environment..." -ForegroundColor Gray

# Check multiple possible VS2022 paths
$vsPaths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

$vsPath = $null
foreach ($path in $vsPaths) {
    if (Test-Path $path) {
        $vsPath = $path
        Write-Host "  Found VS2022: $path" -ForegroundColor Gray
        break
    }
}

if (-not $vsPath) {
    Write-Host "ERROR: Could not find Visual Studio 2022!" -ForegroundColor Red
    Write-Host "Checked paths:" -ForegroundColor Red
    $vsPaths | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

# Source the VS environment
$envOutput = cmd /c "`"$vsPath`" >nul 2>&1 && set" 2>&1
$envOutput | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

# Add Python Scripts to PATH for conan
$pythonScripts = "$env:APPDATA\Python\Python314\Scripts"
if (Test-Path $pythonScripts) {
    $env:PATH = $pythonScripts + ";" + $env:PATH
    Write-Host "  Added Python Scripts to PATH" -ForegroundColor Gray
}

# Add governor wrappers to PATH - THIS IS THE KEY PART
$wrapperDir = "$ToolkitPath\tools\build-governor\bin\wrappers"
$serviceDir = "$ToolkitPath\tools\build-governor\bin\service"

# Build governor if not already built
if (-not (Test-Path "$wrapperDir\cl.exe")) {
    Write-Host "Building governor (first time setup)..." -ForegroundColor Yellow
    Push-Location "$ToolkitPath\tools\build-governor"
    dotnet publish -c Release -o bin 2>&1 | Out-Null
    Pop-Location
}

$env:PATH = "$wrapperDir;$env:PATH"
$env:GOV_SERVICE_PATH = "$serviceDir\Gov.Service.exe"
$env:GOV_DEBUG = "1"

Write-Host ""
Write-Host "========================================================================" -ForegroundColor Green
Write-Host "  Build Governor Configuration" -ForegroundColor Green
Write-Host "========================================================================" -ForegroundColor Green
Write-Host "  Wrappers:     $wrapperDir" -ForegroundColor White
Write-Host "  Service:      $env:GOV_SERVICE_PATH" -ForegroundColor White
Write-Host "  Debug Mode:   ON" -ForegroundColor White
Write-Host ""

# Verify wrapper is in PATH
$clPath = (Get-Command cl.exe | Select-Object -First 1).Source
Write-Host "  cl.exe path:  $clPath" -ForegroundColor $(if ($clPath -like "*wrappers*") { "Green" } else { "Red" })

if ($clPath -notlike "*wrappers*") {
    Write-Host "ERROR: Governor wrapper not in PATH!" -ForegroundColor Red
    exit 1
}

# Check/start governor
$govProc = Get-Process -Name "Gov.Service" -ErrorAction SilentlyContinue
if ($govProc) {
    Write-Host "  Governor:     Running (PID $($govProc.Id))" -ForegroundColor Green
} else {
    Write-Host "  Governor:     Will auto-start on first compilation" -ForegroundColor Yellow
}
Write-Host ""

# Step 1: Conan install
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  Step 1: Conan Install" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan

Set-Location $buildDir

# Check if conan toolchain already exists
$toolchainFile = "build\generators\conan_toolchain.cmake"
if (-not (Test-Path $toolchainFile)) {
    Write-Host "Running conan install..." -ForegroundColor Yellow
    & conan install .. --output-folder . --build=missing --settings build_type=$BuildType
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Conan install failed!" -ForegroundColor Red
        Set-Location ..
        exit 1
    }
} else {
    Write-Host "Conan already configured, skipping..." -ForegroundColor Gray
}

# Step 2: CMake configure
Write-Host ""
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  Step 2: CMake Configure" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan

if (-not (Test-Path "build.ninja")) {
    Write-Host "Running cmake configure..." -ForegroundColor Yellow

    # Use absolute path for toolchain file
    $absoluteToolchain = Join-Path (Get-Location) $toolchainFile

    & cmake -G Ninja .. `
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$absoluteToolchain" `
        "-DCMAKE_BUILD_TYPE=$BuildType" `
        -Dxrpld=ON `
        -Dtests=OFF

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configure failed!" -ForegroundColor Red
        Set-Location ..
        exit 1
    }
} else {
    Write-Host "Already configured, skipping..." -ForegroundColor Gray
}

# Step 3: Build with governor protection
Write-Host ""
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  Step 3: Building rippled (Governor Protected)" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Starting build with -j $Parallel ..." -ForegroundColor Yellow
Write-Host "Governor will monitor memory and throttle if needed." -ForegroundColor Yellow
Write-Host ""

$startTime = Get-Date

# Run the build
& cmake --build . --parallel $Parallel 2>&1 | ForEach-Object {
    # Highlight governor messages
    if ($_ -match '\[gov') {
        Write-Host $_ -ForegroundColor Magenta
    } else {
        Write-Host $_
    }
}

$buildExitCode = $LASTEXITCODE
$duration = (Get-Date) - $startTime

Write-Host ""
Write-Host "========================================================================" -ForegroundColor $(if ($buildExitCode -eq 0) { "Green" } else { "Red" })
Write-Host "  Build $(if ($buildExitCode -eq 0) { 'Completed Successfully' } else { 'FAILED' })" -ForegroundColor $(if ($buildExitCode -eq 0) { "Green" } else { "Red" })
Write-Host "========================================================================" -ForegroundColor $(if ($buildExitCode -eq 0) { "Green" } else { "Red" })
Write-Host ""
Write-Host "  Duration:     $($duration.ToString('hh\:mm\:ss'))" -ForegroundColor White
Write-Host "  Exit Code:    $buildExitCode" -ForegroundColor $(if ($buildExitCode -eq 0) { "Green" } else { "Red" })

# Show governor stats
$govProc = Get-Process -Name "Gov.Service" -ErrorAction SilentlyContinue
if ($govProc) {
    Write-Host ""
    Write-Host "  Governor Stats:" -ForegroundColor Cyan
    Write-Host "    PID:          $($govProc.Id)"
    Write-Host "    Working Set:  $([math]::Round($govProc.WorkingSet64/1MB, 1)) MB"
    Write-Host "    CPU Time:     $([math]::Round($govProc.CPU, 2)) seconds"
}

# Check for output binary (xrpld.exe in newer versions, rippled.exe in older)
$outputBinary = $null
if (Test-Path "xrpld.exe") {
    $outputBinary = "xrpld.exe"
} elseif (Test-Path "rippled.exe") {
    $outputBinary = "rippled.exe"
}

if ($outputBinary) {
    $size = (Get-Item $outputBinary).Length / 1MB
    Write-Host ""
    Write-Host "  Output:       $outputBinary ($([math]::Round($size, 1)) MB)" -ForegroundColor Green
}

Set-Location ..
exit $buildExitCode
