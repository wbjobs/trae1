@echo off
REM Build script for Video Stream Super-Resolution Service (Windows)
REM Requires: CMake, OpenCV, Visual Studio 2019/2022

echo ========================================
echo Video SR Service v1.1 - Windows Build Script
echo Features: EDSR x2, INT8 Quantization,
echo           Motion Compensation, 3D Temporal Conv
echo ========================================

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found in PATH
    exit /b 1
)

set BUILD_DIR=build
set OPENCV_DIR=C:\opencv\build

if not exist %BUILDILD% mkdir %BUILDILD%

echo [1/3] Configuring CMake project...
cd %BUILDILD%
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DOpenCV_DIR=%OPENCV_DIR% ^
    -DUSE_HLS_SIMULATION=ON ^
    -DUSE_CPU_REFERENCE=ON ^
    -DENABLE_MOTION_COMPENSATION=ON

if errorlevel 1 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

echo [2/3] Building project...
cmake --build . --config Release

if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

echo [3/3] Generating default model weights (with temporal fusion and scene models)...
cd ..
python scripts\export_model.py --output models\edsr_int8.bin --num-res-blocks 8 --num-features 64 --generate-all-scenes

echo.
echo ========================================
echo Build complete!
echo ========================================
echo Binary: %BUILDILD%\Release\video_sr_service.exe
echo Model: models\edsr_int8.bin
echo.
echo Run with motion compensation:
echo   %BUILDILD%\Release\video_sr_service.exe --motion-compensation --fps-stats
echo.
echo Run with scene classification and dashboard:
echo   %BUILDILD%\Release\video_sr_service.exe --scene-classification --dashboard --fps-stats
echo.
echo Run with forced scene model:
echo   %BUILDILD%\Release\video_sr_service.exe --force-scene sports --motion-compensation
echo.
echo Run with custom model:
echo   %BUILDILD%\Release\video_sr_service.exe --custom-model models\custom\my_model.bin --custom-model-name "MyModel"
echo.
echo Standard mode:
echo   %BUILDILD%\Release\video_sr_service.exe --fps-stats
echo ========================================