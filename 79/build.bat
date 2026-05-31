@echo off
REM ============================================================
REM  Build script for USB Firmware Batch Flasher (Windows MSVC)
REM  Requires: Visual Studio Build Tools + libusb
REM ============================================================

setlocal

set CC=cl
set CFLAGS=/W3 /O2 /std:c11 /Fe:usb_flash.exe
set INCLUDES=/Iinclude
set LIBS=libusb-1.0.lib pthreadVC2.lib

if "%LIBUSB_DIR%"=="" (
    echo Error: Please set LIBUSB_DIR to your libusb installation path
    echo Example: set LIBUSB_DIR=C:\libusb
    exit /b 1
)

set INCLUDES=%INCLUDES% /I"%LIBUSB_DIR%\include\libusb-1.0"
set LIBDIR=/LIBPATH:"%LIBUSB_DIR%\MS64\dll"

echo ============================================
echo  USB Firmware Batch Flasher - Build
echo ============================================
echo.

%CC% %CFLAGS% %INCLUDES% src\main.c src\logger.c src\progress.c src\firmware.c src\usb_handler.c src\config.c /link %LIBDIR% %LIBS%

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
) else (
    echo.
    echo Build failed with error code %ERRORLEVEL%
)

endlocal
