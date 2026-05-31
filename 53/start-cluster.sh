#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT"
go build -o raftkv ./cmd/raftkv

rm -rf data-node1 data-node2 data-node3

./raftkv -id node1 -raft 127.0.0.1:7001 -http 127.0.0.1:8001 -data ./data-node1 -bootstrap &
PID1=$!

sleep 3

./raftkv -id node2 -raft 127.0.0.1:7002 -http 127.0.0.1:8002 -data ./data-node2 -join 127.0.0.1:8001 &
PID2=$!

./raftkv -id node3 -raft 127.0.0.1:7003 -http 127.0.0.1:8003 -data ./data-node3 -join 127.0.0.1:8001 &
PID3=$!

echo "PIDs: $PID1 $PID2 $PID3"
echo "Press Ctrl-C to stop all nodes."
trap "kill $PID1 $PID2 $PID3" INT TERM
wait
