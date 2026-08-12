@echo off
setlocal

rem Removes the CMake build tree. Nothing under res\ or src\ is touched.
set "BUILD_DIR=%~dp0build"

if not exist "%BUILD_DIR%" (
    echo nothing to clean
    exit /b 0
)

rem PowerModeTray.exe cannot be deleted while an instance is running.
tasklist /fi "imagename eq PowerModeTray.exe" /nh 2>nul | find /i "PowerModeTray.exe" >nul
if not errorlevel 1 (
    echo NOTE: PowerModeTray.exe is running; quit it from the tray first if the
    echo       delete below fails.
)

echo == removing "%BUILD_DIR%"
rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" (
    echo ERROR: could not remove "%BUILD_DIR%".
    exit /b 1
)
exit /b 0
