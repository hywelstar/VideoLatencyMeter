@echo off
setlocal

set "FIRMWARE=%~dp0build\VideoLatencyMeter.hex"
set "PROGRAMMER=E:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

if not exist "%FIRMWARE%" (
    echo ERROR: Firmware not found: %FIRMWARE%
    echo Run build.bat first.
    exit /b 1
)

if not exist "%PROGRAMMER%" (
    echo ERROR: STM32CubeProgrammer CLI was not found:
    echo   %PROGRAMMER%
    echo Install STM32CubeProgrammer to E:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer
    exit /b 1
)

pnputil /enum-devices /connected | findstr /I /C:"VID_0483&PID_DF11" >nul
if errorlevel 1 (
    echo ERROR: STM32 DFU bootloader was not detected.
    echo Set BOOT0=3.3V, BOOT1=GND, reset the board, and connect the USB Device port.
    exit /b 1
)

echo DFU device detected. Programming and verifying:
echo   %FIRMWARE%
"%PROGRAMMER%" -c port=usb1 -w "%FIRMWARE%" -v
if errorlevel 1 (
    echo ERROR: Programming failed. Keep BOOT0 high and retry after checking the USB cable.
    exit /b 1
)

echo.
echo Programming and verification succeeded.
echo Set BOOT0 back to GND and press RESET or power-cycle the board to boot the firmware.
endlocal
