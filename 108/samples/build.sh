#!/bin/bash
set -e

cd "$(dirname "$0")"

cargo build --target wasm32-wasi --release

mkdir -p ../bin
cp target/wasm32-wasi/release/hello_wasm.wasm ../bin/hello.wasm

echo "Build complete: bin/hello.wasm"