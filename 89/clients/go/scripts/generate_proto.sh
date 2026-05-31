#!/usr/bin/env bash
# 生成 Go gRPC 代码
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OUT="./proto/columnar/gateway"
mkdir -p "$OUT"

protoc \
  -I"proto" \
  --go_out=./proto --go_opt=paths=source_relative \
  --go-grpc_out=./proto --go-grpc_opt=paths=source_relative \
  "$ROOT/proto/gateway.proto"

echo "Generated in $OUT"
