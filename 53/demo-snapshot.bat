@echo off
REM Snapshot demo.
setlocal
set L=http://127.0.0.1:8001

echo === 1. Current auto-snapshot config ===
curl -s %L%/cluster/snapshot/auto
echo.

echo === 2. Write 100 keys to build up log entries ===
for /l %%i in (1,1,100) do (
  curl -s -X PUT %L%/kv/key%%i -d "value%%i" >nul
)
echo Wrote 100 keys.

echo === 3. List snapshots (should be empty or few) ===
curl -s %L%/cluster/snapshots
echo.

echo === 4. Trigger manual snapshot ===
curl -s -X POST %L%/cluster/snapshot
echo.

echo === 5. List snapshots again ===
curl -s %L%/cluster/snapshots
echo.

echo === 6. Open admin page in browser ===
echo Visit: %L%/cluster/admin
echo.

echo === 7. Check transfer status ===
curl -s %L%/cluster/snapshot/transfers
echo.
