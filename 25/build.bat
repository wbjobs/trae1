@echo off
chcp 65001 >nul
echo ========================================
echo   STL 布尔运算 Web 应用 - 构建脚本
echo ========================================
echo.

echo [1/4] 安装 Rust wasm32 目标...
rustup target add wasm32-unknown-unknown

echo.
echo [2/4] 检查 wasm-pack...
where wasm-pack >nul 2>nul
if errorlevel 1 (
  echo 未找到 wasm-pack，正在安装...
  cargo install wasm-pack
)

echo.
echo [3/4] 编译 WASM 模块...
cd wasm
wasm-pack build --release --target web --out-dir ..\frontend\public\wasm --out-name stl_bool
cd ..

echo.
echo [4/4] 安装前端依赖...
cd frontend
call npm install
cd ..

echo.
echo ========================================
echo   构建完成！
echo ========================================
echo.
echo 启动后端（新开一个终端）：
echo   pip install -r backend\requirements.txt
echo   python run_backend.py
echo.
echo 启动前端：
echo   cd frontend
echo   npm run dev
echo.
echo 浏览器访问: http://localhost:5173
pause
