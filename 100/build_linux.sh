#!/bin/bash
# Build script for Video Stream Super-Resolution Service (Linux)
# Requires: CMake, OpenCV, GCC >= 9.0

set -e

echo "========================================"
echo "Video SR Service - Linux Build Script"
echo "========================================"

BUILD_DIR=build
OPENCV_DIR=${OPENCV_DIR:-/usr/local/share/OpenCV}

mkdir -p $BUILD_DIR
cd $BUILD_DIR

echo "[1/3] Configuring CMake project..."
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DOpenCV_DIR=$OPENCV_DIR \
    -DUSE_HLS_SIMULATION=ON \
    -DUSE_CPU_REFERENCE=ON

echo "[2/3] Building project..."
make -j$(nproc)

cd ..

echo "[3/3] Generating default model weights..."
python3 scripts/export_model.py --output models/edsr_int8.bin \
    --num-res-blocks 8 --num-features 64

echo ""
echo "========================================"
echo "Build complete!"
echo "========================================"
echo "Binary: $BUILD_DIR/video_sr_service"
echo "Model: models/edsr_int8.bin"
echo ""
echo "Run:"
echo "  $BUILD_DIR/video_sr_service --help"
echo "========================================"