@echo off
REM Start 3-node cluster on Windows.
REM Requires: Go 1.21+ installed and on PATH.
REM Usage: start-cluster.bat [build]

setlocal
set ROOT=%~dp0

if "%1"=="build" (
    echo Building...
    cd /d "%ROOT%"
    go build -o raftkv.exe .\cmd\raftkv || goto :err
)

if not exist "%ROOT%\raftkv.exe" (
    echo Building raftkv.exe...
    cd /d "%ROOT%"
    go build -o raftkv.exe .\cmd\raftkv || goto :err
)

echo Cleaning previous data...
if exist "%ROOT%\data-node1" rmdir /s /q "%ROOT%\data-node1"
if exist "%ROOT%\data-node2" rmdir /s /q "%ROOT%\data-node2"
if exist "%ROOT%\data-node3" rmdir /s /q "%ROOT%\data-node3"

echo Starting node1 (bootstrap leader)...
start "node1" cmd /k "%ROOT%\raftkv.exe -id node1 -raft 127.0.0.1:7001 -http 127.0.0.1:8001 -data %ROOT%\data-node1 -bootstrap"

echo Waiting for node1 to become leader...
timeout /t 3 /nobreak >nul

echo Starting node2...
start "node2" cmd /k "%ROOT%\raftkv.exe -id node2 -raft 127.0.0.1:7002 -http 127.0.0.1:8002 -data %ROOT%\data-node2 -join 127.0.0.1:8001"

echo Starting node3...
start "node3" cmd /k "%ROOT%\raftkv.exe -id node3 -raft 127.0.0.1:7003 -http 127.0.0.1:8003 -data %ROOT%\data-node3 -join 127.0.0.1:8001"

echo.
echo Cluster should be up. Example commands:
echo   curl -X PUT http://127.0.0.1:8001/kv/foo -d "bar"
echo   curl http://127.0.0.1:8001/kv/foo
echo   curl http://127.0.0.1:8001/kv/foo?mode=local
echo   curl http://127.0.0.1:8001/cluster/status
goto :eof

:err
echo Build failed.
exit /b 1
