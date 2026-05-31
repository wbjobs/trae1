@echo off
chcp 65001 >nul
echo ========================================
echo       安全文件分享系统 - 一键启动
echo ========================================
echo.

echo [1/4] 检查Node.js环境...
node --version >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误: 未检测到Node.js，请先安装Node.js
    pause
    exit /b 1
)
echo Node.js版本: 
node --version

echo.
echo [2/4] 安装后端依赖...
cd server
if not exist node_modules (
    call npm install
) else (
    echo 依赖已安装，跳过...
)
cd ..

echo.
echo [3/4] 安装前端依赖...
cd client
if not exist node_modules (
    call npm install
) else (
    echo 依赖已安装，跳过...
)
cd ..

echo.
echo [4/4] 启动服务...
echo.
echo ========================================
echo   后端服务: http://localhost:3000
echo   前端页面: http://localhost:5173
echo   按 Ctrl+C 停止所有服务
echo ========================================
echo.

start "Secure Share Server" cmd /k "cd server && npm run dev"
timeout /t 3 /nobreak >nul
start "Secure Share Client" cmd /k "cd client && npm run dev"

echo 服务启动中...
pause
