<#
.SYNOPSIS
    Enables automatic Build Governor startup for current user.

.DESCRIPTION
    This script sets up Build Governor to auto-start when the first
    compiler invocation occurs. No administrator rights required.

    Two options:
    1. On-demand (default): Governor starts automatically when cl.exe/link.exe
       are invoked through the wrappers.
    2. Login startup: Governor starts when you log in.

.PARAMETER Mode
    "ondemand" (default) - Start when first build tool runs
    "login" - Start at user login

.PARAMETER WrapperPath
    Path to wrapper binaries. Default: bin\wrappers relative to script.

.EXAMPLE
    .\enable-autostart.ps1

.EXAMPLE
    .\enable-autostart.ps1 -Mode login
#>

param(
    [ValidateSet("ondemand", "login")]
    [string]$Mode = "ondemand",

    [string]$WrapperPath
)

$ErrorActionPreference = "Stop"

Write-Host "Build Governor Auto-Start Setup" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# Default wrapper path
if (-not $WrapperPath) {
    $WrapperPath = Join-Path $repoRoot "bin\wrappers"
}

# Check if wrappers exist
if (-not (Test-Path (Join-Path $WrapperPath "cl.exe"))) {
    Write-Host "Wrappers not found. Building..." -ForegroundColor Yellow
    Push-Location $repoRoot
    dotnet publish src/Gov.Wrapper.CL -c Release -o "$WrapperPath"
    dotnet publish src/Gov.Wrapper.Link -c Release -o "$WrapperPath"
    Pop-Location
}

# Verify wrappers exist now
if (-not (Test-Path (Join-Path $WrapperPath "cl.exe"))) {
    Write-Error "Failed to build wrappers"
    exit 1
}

Write-Host "Wrapper directory: $WrapperPath" -ForegroundColor Gray

if ($Mode -eq "login") {
    # Add to startup folder
    $startupFolder = [Environment]::GetFolderPath("Startup")
    $shortcutPath = Join-Path $startupFolder "BuildGovernor.lnk"

    $serviceDir = Join-Path $repoRoot "src\Gov.Service"

    # Create shortcut
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = "dotnet"
    $shortcut.Arguments = "run --project `"$serviceDir`" -c Release -- --background"
    $shortcut.WorkingDirectory = $repoRoot
    $shortcut.WindowStyle = 7  # Minimized
    $shortcut.Save()

    Write-Host "Created startup shortcut: $shortcutPath" -ForegroundColor Green
    Write-Host ""
    Write-Host "Governor will start automatically at login." -ForegroundColor Cyan
}

# Add wrappers to user PATH
$currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
$wrapperPathNorm = (Resolve-Path $WrapperPath).Path

if ($currentPath -notlike "*$wrapperPathNorm*") {
    Write-Host "Adding wrappers to user PATH..." -ForegroundColor Gray
    $newPath = "$wrapperPathNorm;$currentPath"
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
    Write-Host "PATH updated. Restart terminal for changes to take effect." -ForegroundColor Yellow
} else {
    Write-Host "Wrappers already in PATH." -ForegroundColor Gray
}

Write-Host ""
Write-Host "SUCCESS: Auto-start enabled!" -ForegroundColor Green
Write-Host ""
Write-Host "How it works:" -ForegroundColor Cyan

if ($Mode -eq "ondemand") {
    Write-Host "  - When you run cmake/msbuild/ninja, cl.exe and link.exe use the wrappers"
    Write-Host "  - Wrappers automatically start the governor if not running"
    Write-Host "  - Governor shuts down after 30 minutes of inactivity"
    Write-Host ""
    Write-Host "  No background processes when you're not building!"
} else {
    Write-Host "  - Governor starts minimized at login"
    Write-Host "  - All builds are automatically protected"
}

Write-Host ""
Write-Host "Test it:" -ForegroundColor Cyan
Write-Host "  cmake --build . --parallel 16"
Write-Host "  # or"
Write-Host "  msbuild /m:16"
Write-Host ""
