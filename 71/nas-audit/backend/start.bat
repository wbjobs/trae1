@echo off
chcp 65001 >nul
echo ============================================
echo   NAS Audit Service - 后端启动
echo ============================================
echo.

cd /d "%~dp0"

if not exist "venv" (
    echo [INFO] 创建Python虚拟环境...
    python -m venv venv
    call venv\Scripts\activate.bat
    echo [INFO] 安装依赖...
    pip install -r requirements.txt
) else (
    call venv\Scripts\activate.bat
)

echo [INFO] 启动NAS审计服务...
python main.py

pause
