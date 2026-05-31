@echo off
chcp 65001 >nul
echo ============================================
echo   BT 网络资源监控系统 - 启动脚本 (Windows)
echo ============================================
echo.

set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..

echo [1/3] 检查 Python 依赖...
pip show flask flask-cors >nul 2>&1
if errorlevel 1 (
    echo 正在安装 Python 依赖...
    pip install -r "%PROJECT_DIR%\backend\api\requirements.txt"
)

echo.
echo [2/3] 构建前端...
if not exist "%PROJECT_DIR%\frontend\node_modules" (
    echo 正在安装前端依赖...
    cd "%PROJECT_DIR%\frontend"
    call npm install
)
if not exist "%PROJECT_DIR%\frontend\dist\index.html" (
    echo 正在构建前端...
    cd "%PROJECT_DIR%\frontend"
    call npm run build
)

echo.
echo [3/3] 启动 API 服务...
cd "%PROJECT_DIR%\backend\api"
start "BT Monitor API" python app.py

echo.
echo ============================================
echo   API 服务已启动: http://127.0.0.1:5000
echo   前端页面可通过 API 服务访问
echo.
echo   如需启动 DHT 爬虫，请编译 C++ 模块:
echo     cd backend\crawler
echo     mkdir build && cd build
echo     cmake .. && cmake --build .
echo     ./bt_crawler --crawl-interval 60
echo ============================================
echo.
pause
