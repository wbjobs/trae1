#!/bin/bash

cd "$(dirname "$0")/.."

if ! command -v bpftool &> /dev/null; then
    echo "Error: bpftool not found. Please install bpftool."
    exit 1
fi

bpftool btf dump file /sys/kernel/btf/vmlinux format c > bpf/vmlinux.h

echo "vmlinux.h generated successfully in bpf/"
