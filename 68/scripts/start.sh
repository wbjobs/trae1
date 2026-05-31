#!/bin/bash
# DDoS Defense Gateway Startup Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

P4_DIR="${PROJECT_DIR}/p4"
P4INFO_PATH="${P4_DIR}/ddos_defense.p4info"
BMV2_JSON_PATH="${P4_DIR}/ddos_defense.json"

GRPC_ADDR="${P4GRPC_ADDR:-localhost:50051}"
DEVICE_ID="${P4DEVICE_ID:-0}"
API_HOST="${API_HOST:-0.0.0.0}"
API_PORT="${API_PORT:-8080}"

echo "=== DDoS Defense Gateway ==="
echo "Project Directory: ${PROJECT_DIR}"
echo "gRPC Address: ${GRPC_ADDR}"
echo "API Endpoint: http://${API_HOST}:${API_PORT}"
echo ""

if [ ! -f "${P4INFO_PATH}" ]; then
    echo "Error: P4Info file not found: ${P4INFO_PATH}"
    echo "Please compile the P4 program first:"
    echo "  p4c --target bmv2 --arch v1model -o ${P4_DIR}/ ${P4_DIR}/ddos_defense.p4"
    exit 1
fi

if [ ! -f "${BMV2_JSON_PATH}" ]; then
    echo "Error: BMv2 JSON file not found: ${BMV2_JSON_PATH}"
    echo "Please compile the P4 program first:"
    echo "  p4c --target bmv2 --arch v1model -o ${P4_DIR}/ ${P4_DIR}/ddos_defense.p4"
    exit 1
fi

cd "${PROJECT_DIR}"

echo "Starting DDoS Defense Gateway..."
echo ""

exec python -m controller.main \
    --grpc-addr "${GRPC_ADDR}" \
    --device-id "${DEVICE_ID}" \
    --p4info "${P4INFO_PATH}" \
    --bmv2-json "${BMV2_JSON_PATH}" \
    --api-host "${API_HOST}" \
    --api-port "${API_PORT}" \
    "$@"
