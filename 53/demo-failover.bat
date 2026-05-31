@echo off
REM Fault-tolerance demo.
REM Simulates: after 3 consecutive timeouts, client probes other nodes for the new leader.

setlocal
set L=http://127.0.0.1:8001

echo === 1. Detect current leader ===
curl -s %L%/cluster/leader
echo.

echo === 2. PUT foo=bar on node1 ===
curl -s -X PUT %L%/kv/foo -d bar
echo.

echo === 3. Strong GET foo ===
curl -s %L%/kv/foo
echo.

echo === 4. Stop node1 (simulate leader network partition) ===
echo Please close the node1 window now, then press any key.
pause >nul

echo === 5. Retry GET via node2 3 times (client will detect failover) ===
curl -s --max-time 2 http://127.0.0.1:8002/kv/foo 2>&1
echo.
curl -s --max-time 2 http://127.0.0.1:8002/kv/foo 2>&1
echo.
curl -s --max-time 2 http://127.0.0.1:8002/kv/foo 2>&1
echo.

echo === 6. Probe new leader via node2 ===
curl -s http://127.0.0.1:8002/cluster/leader
echo.

echo === 7. Now write via new leader ===
echo   (use the HTTP port of the new leader reported above)
echo.
