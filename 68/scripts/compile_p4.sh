#!/bin/bash
# Compile P4 program for BMv2 target

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
P4_DIR="${PROJECT_DIR}/p4"

P4_FILE="${P4_DIR}/ddos_defense.p4"
OUTPUT_DIR="${P4_DIR}"

echo "=== Compiling P4 Program ==="
echo "Source: ${P4_FILE}"
echo "Output: ${OUTPUT_DIR}"
echo ""

cd "${PROJECT_DIR}"

p4c --target bmv2 --arch v1model -o "${OUTPUT_DIR}/" "${P4_FILE}"

if [ $? -eq 0 ]; then
    echo ""
    echo "Compilation successful!"
    echo "Generated files:"
    ls -la "${OUTPUT_DIR}/"
else
    echo ""
    echo "Compilation failed!"
    exit 1
fi
