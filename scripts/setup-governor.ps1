<#
.SYNOPSIS
    Sets up Build Governor for automatic OOM protection during rippled builds.

.DESCRIPTION
    This script builds and configures the Build Governor, which prevents
    memory exhaustion during parallel C++ compilation.

    After running this script, all builds (cmake, msbuild, ninja) are
    automatically protected - no manual steps required.

.EXAMPLE
    .\scripts\setup-governor.ps1
#>

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║         rippled-windows-debug: Build Governor Setup              ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$governorDir = Join-Path $repoRoot "tools\build-governor"

if (-not (Test-Path $governorDir)) {
    Write-Error "Build Governor not found at $governorDir"
    exit 1
}

Write-Host "Building Build Governor..." -ForegroundColor Yellow
Push-Location $governorDir

# Build everything
dotnet build -c Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    Pop-Location
    exit 1
}

# Publish wrappers and service
$binDir = Join-Path $governorDir "bin"
Write-Host "Publishing wrappers..." -ForegroundColor Gray
dotnet publish src/Gov.Wrapper.CL -c Release -o "$binDir\wrappers" --self-contained -r win-x64
dotnet publish src/Gov.Wrapper.Link -c Release -o "$binDir\wrappers" --self-contained -r win-x64

Write-Host "Publishing service..." -ForegroundColor Gray
dotnet publish src/Gov.Service -c Release -o "$binDir\service" --self-contained -r win-x64

Pop-Location

# Add wrappers to user PATH
$wrapperDir = Join-Path $binDir "wrappers"
$currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")

if ($currentPath -notlike "*$wrapperDir*") {
    Write-Host "Adding wrappers to user PATH..." -ForegroundColor Gray
    $newPath = "$wrapperDir;$currentPath"
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
    Write-Host "PATH updated." -ForegroundColor Green
} else {
    Write-Host "Wrappers already in PATH." -ForegroundColor Gray
}

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║                    Setup Complete!                               ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "Build Governor is now configured for automatic protection." -ForegroundColor Cyan
Write-Host ""
Write-Host "How it works:" -ForegroundColor White
Write-Host "  - When you run cmake/msbuild/ninja, cl.exe uses the wrapper"
Write-Host "  - Wrapper auto-starts the governor if not running"
Write-Host "  - Governor monitors commit charge and throttles if needed"
Write-Host "  - Shuts down automatically after 30 min idle"
Write-Host ""
Write-Host "IMPORTANT: Restart your terminal for PATH changes to take effect." -ForegroundColor Yellow
Write-Host ""
Write-Host "Test it:" -ForegroundColor White
Write-Host "  cmake --build build --parallel 16"
Write-Host ""
