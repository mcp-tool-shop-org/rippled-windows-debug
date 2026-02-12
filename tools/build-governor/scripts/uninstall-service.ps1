#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Uninstalls the Build Governor Windows Service.

.DESCRIPTION
    Stops and removes the Build Governor service. Optionally removes
    installed files.

.PARAMETER RemoveFiles
    If specified, also removes installed files.

.EXAMPLE
    .\uninstall-service.ps1

.EXAMPLE
    .\uninstall-service.ps1 -RemoveFiles
#>

param(
    [switch]$RemoveFiles
)

$ErrorActionPreference = "Stop"

$serviceName = "BuildGovernor"
$installPath = "$env:ProgramFiles\BuildGovernor"

Write-Host "Build Governor Service Uninstaller" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

# Check if service exists
$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue

if ($service) {
    Write-Host "Stopping service..." -ForegroundColor Gray
    Stop-Service -Name $serviceName -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    Write-Host "Removing service..." -ForegroundColor Gray
    sc.exe delete $serviceName | Out-Null
    Write-Host "Service removed." -ForegroundColor Green
} else {
    Write-Host "Service not found (may already be uninstalled)" -ForegroundColor Yellow
}

if ($RemoveFiles -and (Test-Path $installPath)) {
    Write-Host "Removing files from $installPath..." -ForegroundColor Gray
    Remove-Item -Path $installPath -Recurse -Force
    Write-Host "Files removed." -ForegroundColor Green
}

Write-Host ""
Write-Host "Build Governor uninstalled successfully." -ForegroundColor Green
