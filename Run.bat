@echo off
rem ==========================================================================
rem  Input Recorder - convenient launcher (run from the project root).
rem  Builds the Release exe if it is missing, refreshes the root copy, then
rem  launches the app (it minimizes to the system tray).
rem ==========================================================================
setlocal
cd /d "%~dp0"

set "EXE=build\bin\Release\InputRecorder.exe"

if not exist "%EXE%" (
    echo Building Input Recorder ^(Release^)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Config Release
)

if not exist "%EXE%" (
    echo.
    echo Build failed - see the output above.
    pause
    exit /b 1
)

rem Keep a double-clickable copy at the project root, always fresh.
copy /Y "%EXE%" "InputRecorder.exe" >nul

echo Launching Input Recorder... (look for its icon in the system tray)
start "" "InputRecorder.exe"
endlocal
