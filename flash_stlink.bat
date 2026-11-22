@echo off
setlocal

rem Build, program, verify, and reset through an ST-Link SWD probe.
set "ROOT=%~dp0"
set "FIRMWARE=%ROOT%build\VideoLatencyMeter.hex"
set "PROGRAMMER=E:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

if not exist "%PROGRAMMER%" (
    echo ERROR: STM32CubeProgrammer CLI was not found at:
    echo   %PROGRAMMER%
    exit /b 1
)

echo [1/2] Building firmware...
call "%ROOT%build.bat"
if errorlevel 1 (
    echo ERROR: Build failed; firmware was not programmed.
    exit /b 1
)

if not exist "%FIRMWARE%" (
    echo ERROR: Firmware not found: %FIRMWARE%
    exit /b 1
)

echo [2/2] Programming and verifying through ST-Link...
echo   %FIRMWARE%
"%PROGRAMMER%" -c port=SWD mode=UR reset=HWrst -w "%FIRMWARE%" -v -rst
if errorlevel 1 (
    echo WARN: Hardware-reset connection failed; retrying Hot Plug mode.
    echo       This mode works when NRST is not wired to the ST-Link.
    "%PROGRAMMER%" -c port=SWD mode=HotPlug -w "%FIRMWARE%" -v
    if errorlevel 1 (
        echo ERROR: ST-Link programming failed.
        echo Check SWDIO, SWCLK, GND and target power.
        exit /b 1
    )
    echo.
    echo Programming and verification succeeded in Hot Plug mode.
    echo Press the board RESET button or power-cycle it to boot the new firmware.
    endlocal
    exit /b 0
)

echo.
echo Programming and verification succeeded. The MCU has been reset.
endlocal
