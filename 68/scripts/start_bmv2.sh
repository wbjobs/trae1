#!/bin/bash
# Start BMv2 software switch with DDoS defense program

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

P4_DIR="${PROJECT_DIR}/p4"
BMV2_JSON="${P4_DIR}/ddos_defense.json"
GRPC_PORT="${GRPC_PORT:-50051}"

echo "=== Starting BMv2 Switch ==="
echo "P4 JSON: ${BMV2_JSON}"
echo "gRPC Port: ${GRPC_PORT}"
echo ""

if [ ! -f "${BMV2_JSON}" ]; then
    echo "Error: BMv2 JSON not found: ${BMV2_JSON}"
    echo "Compile P4 program first: ./compile_p4.sh"
    exit 1
fi

cd "${PROJECT_DIR}"

simple_switch_grpc \
    --log-file /tmp/bmv2_ddos.log \
    --log-flush \
    --log-level info \
    -i 0@veth0 \
    -i 1@veth2 \
    -i 2@veth4 \
    -i 3@veth6 \
    --thrift-port 9090 \
    --grpc-server-addr 0.0.0.0:${GRPC_PORT} \
    "${BMV2_JSON}"
