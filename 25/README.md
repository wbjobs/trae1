# STL 布尔运算 Web 应用

## 功能
- 上传两个 STL 格式的 3D 模型文件
- 使用 Rust + WebAssembly 在浏览器端执行布尔运算（并集 / 交集 / 差集）
- 采用 **体素化 + SDF + Marching Cubes** 管线，从根源避免共面面片导致的零厚度碎片；对差集额外提供共面修复 Pass
- Three.js 场景实时展示结果，支持鼠标旋转 / 缩放 / 平移
- 运算进度实时显示（进度条 + 消息）
- 后端（FastAPI + SQLite）存储上传的原始模型文件和每次运算的日志

## 目录结构
```
.
├── backend/        FastAPI 后端（SQLite, 文件上传, 日志 API）
├── frontend/       Vue3 + TypeScript + Vite + Three.js
├── wasm/           Rust crate（wasm-bindgen），布尔运算核心
└── build.bat       Windows 一键构建脚本
```

## 环境要求
- Rust (stable) + `wasm32-unknown-unknown` target
- wasm-pack
- Node.js 18+
- Python 3.10+

## 构建（Windows）

```bat
build.bat
```

脚本会：
1. `rustup target add wasm32-unknown-unknown`
2. 自动检测并安装 wasm-pack
3. `wasm-pack build --release --target web`，产物输出到 `frontend/public/wasm/`
4. `npm install` 前端依赖

或者手动：
```powershell
cd wasm
wasm-pack build --release --target web --out-dir ..\frontend\public\wasm --out-name stl_bool
cd ..\frontend
npm install
```

## 运行

**终端 1 - 后端：**
```powershell
pip install -r backend\requirements.txt
python run_backend.py
```
后端监听 `http://localhost:8000`，Swagger 文档在 `http://localhost:8000/docs`。

**终端 2 - 前端：**
```powershell
cd frontend
npm run dev
```
浏览器访问 `http://localhost:5173`。

## 关键实现

### 差集共面修复 (`wasm/src/voxel.rs`)
差集运算后，调用 `resolve_coplanar_artifacts`：
1. 对输出 SDF 在阈值 `t = 0.75 * cell_size` 内的点打标记（疑似共面）
2. 将标记向 6 邻域膨胀 1 圈（连接孤立的薄碎片）
3. 对每个标记点检查邻居 SDF 符号：
   - 若周围全为正/负，则该点向同方向推离 0 等值面 `t/2`，避免产生退化三角
   - 若两侧都有，则根据当前符号进一步向同侧偏移 `t/2`，保证薄带被"拉开"而非被 Marching Cubes 抽出为零厚度片

这种方法避免了 BSP/CSG 中常见的共面退化三角形问题，即使输入模型有完全共面的面片也能产生有效流形网格。

### 体素化 (`wasm/src/voxel.rs`)
- 每个网格角点使用 **射线-三角形求交 + 奇偶缠绕数** 判断内/外
- 距离使用 **最近点到三角形** 的欧氏距离
- 三角网格通过 **空间网格加速结构 (`TriGrid`)** 加速邻域查询
- 两模型使用相同的分辨率，SDF 通过三线性插值组合

### Marching Cubes (`wasm/src/mc.rs` + `wasm/build.rs`)
构建期生成 `mc_tables.rs`（包含经典的 256 项 EDGE_TABLE / TRI_TABLE），运行时遍历网格、插值顶点、三角化等值面。

### 进度反馈 (`wasm/src/lib.rs`)
使用 `Mutex<f32>` + `Mutex<String>` 暴露 `get_progress()` / `get_message()`，前端以 120ms 间隔轮询更新进度条。

## API
| 方法 | 路径 | 说明 |
| ---- | ---- | ---- |
| POST | `/api/upload` | 上传 STL，返回 `{filename, url}` |
| GET  | `/api/logs`   | 最近 100 条运算日志 |
| POST | `/api/logs`   | 写入一条运算日志 |
| GET  | `/uploads/<name>` | 静态访问已上传的 STL |

## 调优
前端界面可调整 **分辨率（体素网格数）**：
- 较小模型：80（默认）→ 流畅
- 高精度：120~160 → 更精细但耗时
- 超大模型：32~40 → 快速预览
