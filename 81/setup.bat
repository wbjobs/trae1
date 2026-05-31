@echo off
chcp 65001 >nul
echo ========================================
echo   语音命令词识别系统 - 构建脚本
echo ========================================
echo.

echo [1/5] 检查 Node.js...
node --version >nul 2>&1
if errorlevel 1 (
    echo   未找到 Node.js，请先安装 Node.js 18+
    echo   下载地址: https://nodejs.org/
    pause
    exit /b 1
)
echo   Node.js 已安装

echo.
echo [2/5] 检查 Rust...
rustc --version >nul 2>&1
if errorlevel 1 (
    echo   未找到 Rust，请先安装 Rust
    echo   安装命令: winget install Rustlang.Rustup
    pause
    exit /b 1
)
echo   Rust 已安装

echo.
echo [3/5] 安装 Tauri CLI...
npx --yes @tauri-apps/cli@2 --version >nul 2>&1
echo   Tauri CLI 就绪

echo.
echo [4/5] 创建 models 目录...
if not exist "models" mkdir models
echo   models 目录已就绪

echo.
echo [5/5] 项目环境检查完成!
echo.
echo ========================================
echo   可用命令:
echo.
echo   开发运行:   npm run dev
echo   构建发布:   npm run build
echo   模拟服务:   python smart_home_server.py
echo.
echo ========================================
echo.
echo 是否立即启动开发模式? (y/n)
set /p choice=
if /i "%choice%"=="y" (
    npm run dev
)
pause
