#!/bin/bash
# ============================================================
#  Build script for USB Firmware Batch Flasher (MSYS2/MinGW)
#  Requires: mingw-w64-gcc, mingw-w64-libusb
#  Optional: mingw-w64-libsodium (for Ed25519 signatures)
# ============================================================

set -e

CC="gcc"
CFLAGS="-Wall -Wextra -O2 -std=c11 -mconsole"
INCLUDES="-Iinclude"
LDFLAGS="-lusb-1.0 -lpthread"
TARGET="usb_flash.exe"

USE_SODIUM=0

for arg in "$@"; do
    case $arg in
        --sodium)
            USE_SODIUM=1
            ;;
        --help)
            echo "Usage: ./build.sh [options]"
            echo ""
            echo "Options:"
            echo "  --sodium    Build with libsodium support"
            echo "  --help      Show this help"
            exit 0
            ;;
    esac
done

echo "============================================"
echo "  USB Firmware Batch Flasher v3.0 - Build"
echo "============================================"
echo ""

if [ "$USE_SODIUM" -eq 1 ]; then
    echo "[*] Building with libsodium support..."
    CFLAGS="$CFLAGS -DUSE_LIBSODIUM"
    LDFLAGS="$LDFLAGS -lsodium"
else
    echo "[*] Building without libsodium (using built-in Ed25519)"
fi

SRCS="src/main.c src/logger.c src/progress.c src/firmware.c src/usb_handler.c src/config.c src/sign.c src/suit.c"

$CC $CFLAGS $INCLUDES -o $TARGET $SRCS $LDFLAGS

if [ $? -eq 0 ]; then
    echo ""
    echo "Build successful: $TARGET"
    echo ""
    echo "Commands:"
    echo "  ./$TARGET --help"
    echo "  ./$TARGET --gen-key"
    echo "  ./$TARGET --sign-firmware firmware.bin"
    echo "  ./$TARGET --verify-firmware firmware.bin"
    echo "  ./$TARGET flash firmware.bin"
else
    echo ""
    echo "Build failed!"
    exit 1
fi
