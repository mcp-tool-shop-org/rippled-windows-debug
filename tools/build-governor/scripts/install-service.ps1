#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs Build Governor as a Windows Service.

.DESCRIPTION
    This script installs the Build Governor service so it runs automatically
    at system startup, protecting all builds without manual intervention.

.PARAMETER InstallPath
    Path where Governor is installed. Default: $env:ProgramFiles\BuildGovernor

.EXAMPLE
    .\install-service.ps1

.EXAMPLE
    .\install-service.ps1 -InstallPath "C:\Tools\Governor"
#>

param(
    [string]$InstallPath = "$env:ProgramFiles\BuildGovernor"
)

$ErrorActionPreference = "Stop"

$serviceName = "BuildGovernor"
$displayName = "Build Reliability Governor"
$description = "Prevents C++ build memory exhaustion by managing compiler concurrency. Automatic protection for cmake, msbuild, ninja builds."

Write-Host "Build Governor Service Installer" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

# Check if already installed
$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($existingService) {
    Write-Host "Service already exists. Stopping..." -ForegroundColor Yellow
    Stop-Service -Name $serviceName -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    Write-Host "Removing existing service..." -ForegroundColor Yellow
    sc.exe delete $serviceName | Out-Null
    Start-Sleep -Seconds 2
}

# Create install directory
if (-not (Test-Path $InstallPath)) {
    Write-Host "Creating install directory: $InstallPath" -ForegroundColor Gray
    New-Item -ItemType Directory -Path $InstallPath -Force | Out-Null
}

# Find source files
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# Check if published files exist
$publishDir = Join-Path $repoRoot "bin\service"
$serviceExe = Join-Path $publishDir "Gov.Service.exe"

if (-not (Test-Path $serviceExe)) {
    Write-Host "Publishing Gov.Service..." -ForegroundColor Gray
    Push-Location $repoRoot
    dotnet publish src/Gov.Service -c Release -o "$publishDir" --self-contained -r win-x64
    Pop-Location

    if (-not (Test-Path $serviceExe)) {
        Write-Error "Failed to publish Gov.Service"
        exit 1
    }
}

# Copy files to install path
Write-Host "Copying files to $InstallPath..." -ForegroundColor Gray
Copy-Item "$publishDir\*" -Destination $InstallPath -Recurse -Force

# Also copy wrappers
$wrapperDir = Join-Path $repoRoot "bin\wrappers"
$targetWrapperDir = Join-Path $InstallPath "wrappers"

if (Test-Path $wrapperDir) {
    if (-not (Test-Path $targetWrapperDir)) {
        New-Item -ItemType Directory -Path $targetWrapperDir -Force | Out-Null
    }
    Copy-Item "$wrapperDir\*" -Destination $targetWrapperDir -Recurse -Force
}

# Create the Windows Service
$serviceExePath = Join-Path $InstallPath "Gov.Service.exe"
Write-Host "Creating service: $serviceName" -ForegroundColor Gray

New-Service -Name $serviceName `
    -BinaryPathName "`"$serviceExePath`" --service" `
    -DisplayName $displayName `
    -Description $description `
    -StartupType Automatic | Out-Null

# Start the service
Write-Host "Starting service..." -ForegroundColor Gray
Start-Service -Name $serviceName

# Verify
$service = Get-Service -Name $serviceName
if ($service.Status -eq "Running") {
    Write-Host ""
    Write-Host "SUCCESS: Build Governor installed and running!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Service Details:" -ForegroundColor Cyan
    Write-Host "  Name:     $serviceName"
    Write-Host "  Status:   $($service.Status)"
    Write-Host "  Startup:  Automatic"
    Write-Host "  Path:     $InstallPath"
    Write-Host ""
    Write-Host "Next Steps:" -ForegroundColor Cyan
    Write-Host "  1. Add wrappers to PATH (optional for automatic protection):"
    Write-Host "     `$env:PATH = `"$targetWrapperDir;`$env:PATH`""
    Write-Host ""
    Write-Host "  2. Or use 'gov run' for per-build protection:"
    Write-Host "     gov run -- cmake --build . --parallel 16"
    Write-Host ""
} else {
    Write-Error "Service installed but not running. Check Event Viewer for details."
}
