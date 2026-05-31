@echo off
REM Audio Fingerprint CLI Build Script
REM Requirements: CMake, libsoundio, FFTW3, SQLite3, FFmpeg (optional for streaming)

echo ============================================
echo   Audio Fingerprint Recognition - Build
echo   (v3.0 with Streaming Support)
echo ============================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found. Please install CMake first.
    echo Download: https://cmake.org/download/
    exit /b 1
)

REM Create build directory
if not exist build mkdir build
cd build

REM Configure with CMake
echo [1/3] Configuring project...
echo.
echo Streaming will be enabled if FFmpeg is detected.
echo To disable streaming, add: -DENABLE_STREAMING=OFF
echo.

cmake .. -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    echo Please ensure libsoundio, FFTW3, and SQLite3 are installed.
    echo For streaming support, ensure FFmpeg development libraries are installed.
    cd ..
    exit /b 1
)

REM Build
echo.
echo [2/3] Building project...
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    cd ..
    exit /b 1
)

REM Install
echo.
echo [3/3] Installing...
cmake --install . --config Release

echo.
echo ============================================
echo   Build Complete!
echo ============================================
echo.
echo Executables:
echo   afp_cli      - Audio recognition CLI (microphone + streaming)
echo   afp_builddb  - Offline fingerprint database builder
echo.
echo Usage:
echo   REM Build database with 100 songs
echo   afp_builddb --db fingerprints.db
echo.
echo   REM Microphone recognition
echo   afp_cli --db fingerprints.db
echo.
echo   REM Streaming recognition (requires FFmpeg)
echo   afp_cli --db fingerprints.db --stream http://example.com/stream.m3u8
echo   afp_cli --db fingerprints.db --stream rtmp://example.com/live --srt output.srt
echo.

cd ..
