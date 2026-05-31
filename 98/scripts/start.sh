#!/bin/bash
set -e

echo "============================================"
echo "  BT 网络资源监控系统 - 启动脚本"
echo "============================================"
echo ""

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "[1/4] 检查 Python 依赖..."
pip3 show flask flask-cors >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "正在安装 Python 依赖..."
    pip3 install -r "$PROJECT_DIR/backend/api/requirements.txt"
fi

echo ""
echo "[2/4] 安装前端依赖..."
if [ ! -d "$PROJECT_DIR/frontend/node_modules" ]; then
    echo "正在安装前端依赖..."
    cd "$PROJECT_DIR/frontend"
    npm install
fi

echo ""
echo "[3/4] 构建前端..."
if [ ! -f "$PROJECT_DIR/frontend/dist/index.html" ]; then
    echo "正在构建前端..."
    cd "$PROJECT_DIR/frontend"
    npm run build
fi

echo ""
echo "[4/4] 启动 API 服务..."
cd "$PROJECT_DIR/backend/api"

CRAWL_INTERVAL=${CRAWL_INTERVAL:-60}
API_HOST=${API_HOST:-0.0.0.0}
API_PORT=${API_PORT:-5000}

echo "爬虫间隔: ${CRAWL_INTERVAL}s"
echo "API 地址: ${API_HOST}:${API_PORT}"

python3 app.py &
API_PID=$!

echo ""
echo "============================================"
echo "  API 服务已启动: http://${API_HOST}:${API_PORT}"
echo "  API PID: $API_PID"
echo ""
echo "  如需启动 DHT 爬虫，请编译 C++ 模块:"
echo "    cd backend/crawler"
echo "    mkdir build && cd build"
echo "    cmake .. && cmake --build ."
echo "    ./bt_crawler --crawl-interval ${CRAWL_INTERVAL}"
echo "============================================"
echo ""

wait $API_PID
