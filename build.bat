@echo off
setlocal

set "TOOLCHAIN_BIN=E:\Program Files\GNU Arm Embedded Toolchain\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin"

if not exist "%TOOLCHAIN_BIN%\arm-none-eabi-gcc.exe" (
    echo ERROR: ARM GCC was not found at:
    echo   %TOOLCHAIN_BIN%
    echo Update TOOLCHAIN_BIN in build.bat if the toolchain was installed elsewhere.
    exit /b 1
)

where mingw32-make.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: mingw32-make.exe was not found on PATH.
    exit /b 1
)

set "PATH=%TOOLCHAIN_BIN%;%PATH%"
set "BUILD_JOBS=%NUMBER_OF_PROCESSORS%"
if "%BUILD_JOBS%"=="" set "BUILD_JOBS=1"
echo Building VideoLatencyMeter with %BUILD_JOBS% parallel jobs...
mingw32-make clean
if errorlevel 1 exit /b 1

mingw32-make -j%BUILD_JOBS% all
if errorlevel 1 exit /b 1

echo.
echo Build succeeded. Output files are in build\
endlocal
