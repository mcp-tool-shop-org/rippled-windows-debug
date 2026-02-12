@echo off
REM get_git_info.bat
REM Captures git information and outputs compiler flags
REM
REM Usage:
REM   for /f "tokens=*" %%i in ('scripts\get_git_info.bat') do set GIT_FLAGS=%%i
REM   cl %GIT_FLAGS% /EHsc your_file.cpp
REM
REM Or directly:
REM   cl /DGIT_COMMIT_HASH="abc123" ... your_file.cpp

setlocal enabledelayedexpansion

REM Get commit hash (short)
for /f "tokens=*" %%i in ('git rev-parse --short=12 HEAD 2^>nul') do set GIT_HASH=%%i
if not defined GIT_HASH set GIT_HASH=unknown

REM Get branch name
for /f "tokens=*" %%i in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set GIT_BRANCH=%%i
if not defined GIT_BRANCH set GIT_BRANCH=unknown

REM Check if dirty
set GIT_DIRTY=0
for /f "tokens=*" %%i in ('git status --porcelain 2^>nul') do set GIT_DIRTY=1

REM Get commit date
for /f "tokens=*" %%i in ('git log -1 --format^=%%cs 2^>nul') do set GIT_DATE=%%i
if not defined GIT_DATE set GIT_DATE=unknown

REM Get describe
for /f "tokens=*" %%i in ('git describe --tags --always --dirty 2^>nul') do set GIT_DESC=%%i
if not defined GIT_DESC set GIT_DESC=unknown

REM Output compiler flags
echo /DGIT_COMMIT_HASH=\"%GIT_HASH%\" /DGIT_BRANCH=\"%GIT_BRANCH%\" /DGIT_DIRTY=%GIT_DIRTY% /DGIT_COMMIT_DATE=\"%GIT_DATE%\" /DGIT_DESCRIBE=\"%GIT_DESC%\"

endlocal
