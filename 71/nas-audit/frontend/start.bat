@echo off
chcp 65001 >nul
echo ============================================
echo   NAS Audit Dashboard - 前端启动
echo ============================================
echo.

cd /d "%~dp0"

if not exist "node_modules" (
    echo [INFO] 安装npm依赖...
    npm install
)

echo [INFO] 启动前端开发服务器...
echo [INFO] 访问地址: http://localhost:3000
echo.
npm start

pause
