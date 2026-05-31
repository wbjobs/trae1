#!/bin/bash
# 生成 Enclave 签名密钥
set -e
mkdir -p "$(dirname "$0")"
openssl genrsa -out "$(dirname "$0")/enclave_key.pem" -3 3072
openssl rsa -in "$(dirname "$0")/enclave_key.pem" -pubout -out "$(dirname "$0")/enclave_public.pem"
echo "Key pair generated."
