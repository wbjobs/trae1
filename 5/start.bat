@echo off
echo ============================================
echo  前端性能监控溯源系统 - 启动脚本
echo ============================================
echo.

echo [1/3] 检查Python环境...
python --version
if errorlevel 1 (
    echo 错误: 未检测到Python，请先安装Python 3.8+
    pause
    exit /b 1
)

echo.
echo [2/3] 安装依赖...
cd backend
pip install -r requirements.txt

echo.
echo [3/3] 启动后端服务...
echo.
echo ============================================
echo  服务已启动!
echo  API文档: http://localhost:8000/docs
echo  前端页面: 请用浏览器打开 frontend/index.html
echo ============================================
echo.

python main.py
pause
