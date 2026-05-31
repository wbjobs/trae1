#!/bin/bash
#
# SCTP Multi-Path File Transfer - Test Script
#
# This script demonstrates how to use the latency difference mitigation
# features. It assumes you have two network interfaces available.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration - adjust these for your environment
RECV_IP1="192.168.1.200"     # Receiver IP 1
RECV_IP2="10.0.0.200"        # Receiver IP 2
SEND_IP1="192.168.1.100"     # Sender IP 1 (WiFi)
SEND_IP2="10.0.0.50"         # Sender IP 2 (Ethernet)
PORT=9000
TEST_FILE="testfile_1gb.bin"
OUTPUT_DIR="./test_output"

echo -e "${GREEN}=== SCTP Multi-Path Transfer Test ===${NC}"
echo ""

# Check if SCTP module is loaded
echo -e "${YELLOW}[1/6] Checking SCTP kernel module...${NC}"
if ! lsmod | grep -q sctp; then
    echo "Loading SCTP module..."
    sudo modprobe sctp
fi
echo -e "${GREEN}  SCTP module loaded OK${NC}"
echo ""

# Check lksctp-tools
echo -e "${YELLOW}[2/6] Checking lksctp-tools...${NC}"
if ! ldconfig -p | grep -q libsctp; then
    echo -e "${RED}  ERROR: libsctp not found${NC}"
    echo "  Install with: sudo apt-get install lksctp-tools libsctp-dev"
    exit 1
fi
echo -e "${GREEN}  libsctp OK${NC}"
echo ""

# Build the project
echo -e "${YELLOW}[3/6] Building project...${NC}"
make clean
make
echo -e "${GREEN}  Build OK${NC}"
echo ""

# Create test file
echo -e "${YELLOW}[4/6] Creating 1GB test file...${NC}"
if [ ! -f "$TEST_FILE" ]; then
    dd if=/dev/urandom of="$TEST_FILE" bs=1M count=1024 status=progress
fi
ls -lh "$TEST_FILE"
echo -e "${GREEN}  Test file created OK${NC}"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Configure firewall for SCTP
echo -e "${YELLOW}[5/6] Configuring firewall...${NC}"
if command -v iptables &> /dev/null; then
    sudo iptables -A INPUT -p sctp --dport $PORT -j ACCEPT 2>/dev/null || true
    sudo iptables -A OUTPUT -p sctp --sport $PORT -j ACCEPT 2>/dev/null || true
fi
echo -e "${GREEN}  Firewall configured OK${NC}"
echo ""

# Scenario 1: Standard test with default latency threshold
echo -e "${GREEN}=== Test Scenario 1: Default Latency Threshold (40ms) ===${NC}"
echo ""
echo "Receiver command:"
echo "  ./sctp_transfer recv -p $PORT -R -b 0.0.0.0 $OUTPUT_DIR"
echo ""
echo "Sender command (WiFi + Ethernet):"
echo "  ./sctp_transfer send -p $PORT -R --latency-diff 40 \\"
echo "    -l $SEND_IP1,$SEND_IP2 $TEST_FILE $RECV_IP1"
echo ""

# Scenario 2: High latency difference (e.g., WiFi:10ms, Ethernet:50ms)
echo -e "${YELLOW}=== Test Scenario 2: High Latency Difference ===${NC}"
echo ""
echo "When paths have large latency differences (>40ms), use a"
echo "higher threshold to allow more aggressive load balancing:"
echo ""
echo "  ./sctp_transfer send -p $PORT -R --latency-diff 80 \\"
echo "    -l $SEND_IP1,$SEND_IP2 $TEST_FILE $RECV_IP1"
echo ""

# Scenario 3: Very conservative - small blocks on slow paths
echo -e "${YELLOW}=== Test Scenario 3: Conservative Scheduling ===${NC}"
echo ""
echo "For very unstable links, use a low threshold to aggressively"
echo "reduce chunk sizes on slow paths and minimize reorder buffer usage:"
echo ""
echo "  ./sctp_transfer send -p $PORT -R --latency-diff 20 \\"
echo "    -l $SEND_IP1,$SEND_IP2 $TEST_FILE $RECV_IP1"
echo ""

echo -e "${GREEN}=== Key Features Enabled ===${NC}"
echo ""
echo "  1. RTT Measurement:"
echo "     - Probes each path every 500ms"
echo "     - 10-sample moving average"
echo "     - Displayed per-path in ms"
echo ""
echo "  2. Latency-Aware Scheduling:"
echo "     - Path selection weighted by bandwidth * RTT score"
echo "     - Slow paths get smaller chunks"
echo "     - Configurable via --latency-diff MS"
echo ""
echo "  3. Rate Limiting:"
echo "     - RTT > threshold: rate limit to 30-80% of available bw"
echo "     - Thresholds: 1x = size reduction, 2x = max rate limit"
echo ""
echo "  4. Reorder Buffer (Receiver):"
echo "     - $REORDER_BUFFER_SIZE entries"
echo "     - Sorts chunks by ID before writing"
echo "     - $NACK_TIMEOUT_MS ms timeout for missing chunks"
echo ""
echo "  5. NACK-based Retransmission:"
echo "     - Detects missing chunks via buffer gaps"
echo "     - Sends NACK with list of missing IDs"
echo "     - Sender retransmits via best available path"
echo ""

echo -e "${YELLOW}=== To Run The Test ===${NC}"
echo ""
echo "Terminal 1 (Receiver):"
echo "  ./sctp_transfer recv -p $PORT -R -b 0.0.0.0 $OUTPUT_DIR"
echo ""
echo "Terminal 2 (Sender):"
echo "  ./sctp_transfer send -p $PORT -R --latency-diff 40 \\"
echo "    -l $SEND_IP1,$SEND_IP2 $TEST_FILE $RECV_IP1"
echo ""
echo -e "${YELLOW}=== Expected Output ===${NC}"
echo ""
echo "You will see per-path statistics including:"
echo "  - RTT in ms (e.g., WiFi: 8.5ms, Ethernet: 52.3ms)"
echo "  - Speed in MB/s"
echo "  - State (HEALTHY, SLOW, DEGRADED, DOWN)"
echo "  - Reorder buffer occupancy (receiver side)"
echo ""

# Cleanup instructions
echo -e "${YELLOW}=== Cleanup ===${NC}"
echo ""
echo "To remove test files:"
echo "  rm -f $TEST_FILE $TEST_FILE.resume"
echo "  rm -rf $OUTPUT_DIR"
echo "  make clean"
echo ""

echo -e "${GREEN}=== Done ===${NC}"
