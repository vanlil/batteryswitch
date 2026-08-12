@echo off
setlocal

rem ---------------------------------------------------------------------------
rem Tool locations. Both are overridable from the environment; nothing else in
rem this script hard-codes a path.
rem ---------------------------------------------------------------------------
if not defined VS_ROOT set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
if not defined CMAKE   set "CMAKE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set "PRESET=vs2026"
set "SRC_DIR=%~dp0"
set "BUILD_DIR=%~dp0build"

rem Usage: build [Release|Debug]
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i "%CONFIG%"=="debug"   set "CONFIG=Debug"
if not "%CONFIG%"=="Release" if not "%CONFIG%"=="Debug" (
    echo ERROR: unknown configuration "%CONFIG%" ^(expected Release or Debug^).
    exit /b 1
)

if not exist "%CMAKE%" (
    echo ERROR: cmake not found at "%CMAKE%".
    echo        Set VS_ROOT to your Visual Studio install, or CMAKE to a cmake.exe.
    exit /b 1
)

rem Configure once; the generator regenerates itself when CMakeLists.txt changes.
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo == configure [%PRESET%]
    "%CMAKE%" --preset %PRESET% -S "%SRC_DIR%."
    if errorlevel 1 exit /b 1
)

echo == build [%CONFIG%]
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 exit /b 1

echo.
echo built: %BUILD_DIR%\%CONFIG%\PowerModeTray.exe
exit /b 0
