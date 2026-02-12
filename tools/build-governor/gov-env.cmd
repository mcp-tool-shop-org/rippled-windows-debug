@echo off
REM Build Governor Environment Setup
REM Prepends wrapper directory to PATH so cl.exe and link.exe are intercepted
REM
REM Usage:
REM   1. Build the wrappers first: dotnet build -c Release
REM   2. Run this script: gov-env.cmd
REM   3. Then run your build as normal (cmake, msbuild, etc.)

setlocal

REM Find wrapper directory
set WRAPPER_DIR=%~dp0bin\wrappers

if not exist "%WRAPPER_DIR%\cl.exe" (
    echo.
    echo [Build Governor] Wrappers not found. Building...
    echo.
    pushd %~dp0
    dotnet publish src\Gov.Wrapper.CL -c Release -o bin\wrappers
    dotnet publish src\Gov.Wrapper.Link -c Release -o bin\wrappers
    popd
)

REM Check if governor is running
echo.
echo [Build Governor] Checking governor status...
dotnet run --project "%~dp0src\Gov.Service" -- --status 2>nul
if errorlevel 1 (
    echo.
    echo [Build Governor] Governor not running. Start it in another terminal:
    echo     dotnet run --project "%~dp0src\Gov.Service"
    echo.
)

REM Set up environment
echo.
echo [Build Governor] Setting up environment...
echo   Wrapper dir: %WRAPPER_DIR%
echo.

endlocal & (
    set "PATH=%~dp0bin\wrappers;%PATH%"
    set "GOV_ENABLED=1"
)

echo [Build Governor] Environment ready. Run your build commands now.
echo   cl.exe and link.exe are now governed.
echo.
