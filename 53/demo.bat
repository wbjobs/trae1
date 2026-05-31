@echo off
setlocal
set ROOT=%~dp0

echo === Leader ===
curl -s http://127.0.0.1:8001/cluster/leader
echo.

echo === Peers ===
curl -s http://127.0.0.1:8001/cluster/peers
echo.

echo === Node1 status ===
curl -s http://127.0.0.1:8001/cluster/status
echo.

echo === PUT foo=bar ===
curl -s -X PUT http://127.0.0.1:8001/kv/foo -d bar
echo.

echo === Strong GET foo ===
curl -s http://127.0.0.1:8001/kv/foo
echo.

echo === Local GET foo on node2 ===
curl -s "http://127.0.0.1:8002/kv/foo?mode=local"
echo.

echo === DELETE foo ===
curl -s -X DELETE http://127.0.0.1:8001/kv/foo
echo.
