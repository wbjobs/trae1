"""Generate Python gRPC code from gateway.proto.

Usage:
    cd clients/python
    python -m columnar_client._codegen
"""

from __future__ import annotations

import os
import subprocess
import sys


def generate(proto_dir: str, out_dir: str) -> None:
    from grpc_tools import protoc  # type: ignore

    os.makedirs(out_dir, exist_ok=True)
    rc = protoc.main(
        [
            "grpc_tools.protoc",
            "-I",
            proto_dir,
            f"--python_out={out_dir}",
            f"--grpc_python_out={out_dir}",
            os.path.join(proto_dir, "gateway.proto"),
        ]
    )
    if rc != 0:
        raise RuntimeError(f"protoc failed with {rc}")


if __name__ == "__main__":
    # 默认：proto 在 ../../proto；输出到当前包目录
    here = os.path.dirname(os.path.abspath(__file__))
    proto_dir = os.path.abspath(os.path.join(here, "..", "..", "proto"))
    generate(proto_dir, here)
    print("Generated in", here)
