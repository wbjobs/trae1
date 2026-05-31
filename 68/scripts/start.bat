@echo off
REM DDoS Defense Gateway Startup Script (Windows)

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."

set "P4_DIR=%PROJECT_DIR%\p4"
set "P4INFO_PATH=%P4_DIR%\ddos_defense.p4info"
set "BMV2_JSON_PATH=%P4_DIR%\ddos_defense.json"

set "GRPC_ADDR=%P4GRPC_ADDR:localhost:50051=%"
if "%GRPC_ADDR%"=="" set "GRPC_ADDR=localhost:50051"

set "DEVICE_ID=%P4DEVICE_ID:0=%"
if "%DEVICE_ID%"=="" set "DEVICE_ID=0"

set "API_HOST=%API_HOST:0.0.0.0=%"
if "%API_HOST%"=="" set "API_HOST=0.0.0.0"

set "API_PORT=%API_PORT:8080=%"
if "%API_PORT%"=="" set "API_PORT=8080"

echo === DDoS Defense Gateway ===
echo Project Directory: %PROJECT_DIR%
echo gRPC Address: %GRPC_ADDR%
echo API Endpoint: http://%API_HOST%:%API_PORT%
echo.

if not exist "%P4INFO_PATH%" (
    echo Error: P4Info file not found: %P4INFO_PATH%
    echo Please compile the P4 program first:
    echo   p4c --target bmv2 --arch v1model -o "%P4_DIR%\" "%P4_DIR%\ddos_defense.p4"
    pause
    exit /b 1
)

if not exist "%BMV2_JSON_PATH%" (
    echo Error: BMv2 JSON file not found: %BMV2_JSON_PATH%
    echo Please compile the P4 program first:
    echo   p4c --target bmv2 --arch v1model -o "%P4_DIR%\" "%P4_DIR%\ddos_defense.p4"
    pause
    exit /b 1
)

cd /d "%PROJECT_DIR%"

echo Starting DDoS Defense Gateway...
echo.

python -m controller.main ^
    --grpc-addr "%GRPC_ADDR%" ^
    --device-id "%DEVICE_ID%" ^
    --p4info "%P4INFO_PATH%" ^
    --bmv2-json "%BMV2_JSON_PATH%" ^
    --api-host "%API_HOST%" ^
    --api-port "%API_PORT%" ^
    %*

endlocal
