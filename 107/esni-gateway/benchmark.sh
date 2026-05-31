#!/bin/bash

# Performance benchmark script for ESNI Gateway

echo "ESNI Gateway Performance Benchmark"
echo "===================================="

# Check if the binary exists
if [ ! -f "./target/release/esni-gateway" ]; then
    echo "Error: Binary not found. Please run 'cargo build --release' first."
    exit 1
fi

# Start the gateway in background
echo "Starting ESNI Gateway..."
./target/release/esni-gateway --listen 127.0.0.1:8443 --workers 4 &
GATEWAY_PID=$!

# Wait for gateway to start
sleep 2

# Check if gateway is running
if ! kill -0 $GATEWAY_PID 2>/dev/null; then
    echo "Error: Failed to start gateway"
    exit 1
fi

echo "Gateway started with PID $GATEWAY_PID"

# Run benchmark
echo ""
echo "Running benchmark..."
echo ""

# Test 1: Basic connectivity
echo "Test 1: Basic connectivity"
echo "---------------------------"
curl -k https://127.0.0.1:8443/ 2>&1 | head -5

# Test 2: Metrics endpoint
echo ""
echo "Test 2: Metrics endpoint"
echo "------------------------"
curl http://127.0.0.1:9090/metrics 2>&1 | grep "esni_" | head -10

# Test 3: Connection rate test
echo ""
echo "Test 3: Connection rate test (100 connections)"
echo "----------------------------------------------"
START=$(date +%s%N)
for i in {1..100}; do
    curl -k -s https://127.0.0.1:8443/ > /dev/null 2>&1 &
done
wait
END=$(date +%s%N)
DURATION=$((($END - $START) / 1000000))
echo "Completed 100 connections in ${DURATION}ms"
echo "Rate: $((100000 / $DURATION)) connections per second"

# Cleanup
echo ""
echo "Stopping gateway..."
kill $GATEWAY_PID

echo ""
echo "Benchmark complete!"
