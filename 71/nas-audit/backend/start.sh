#!/bin/bash
echo "============================================"
echo "  NAS Audit Service - 后端启动"
echo "============================================"
echo ""

cd "$(dirname "$0")"

if [ ! -d "venv" ]; then
    echo "[INFO] 创建Python虚拟环境..."
    python3 -m venv venv
    source venv/bin/activate
    echo "[INFO] 安装依赖..."
    pip install -r requirements.txt
else
    source venv/bin/activate
fi

echo "[INFO] 启动NAS审计服务..."
python main.py
