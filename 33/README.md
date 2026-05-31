# 🤚 Gesture Recognizer

基于 Tauri + React + ONNX Runtime 的实时手势识别桌面应用。

- 后端（Rust）：加载 ONNX 模型，内置 WebSocket 服务接收摄像头 JPEG 帧并返回推理结果；支持把录制好的视频保存为 MP4/WebM。
- 前端（React + TypeScript）：调用摄像头，视频画面上叠加识别到的手势标签、置信度和 FPS 等统计信息，支持开始/停止录制。
- 通信：通过本地 WebSocket 双向传递图像帧、推理结果和帧率统计。

## ✨ 功能

1. 摄像头实时预览 + 画面叠加显示识别到的手势标签与置信度。
2. 后端 ONNX Runtime 推理，目标单帧耗时 < 50ms。
3. WebSocket 双向通信：
   - 前端 → 后端：JPEG 帧（二进制）。
   - 后端 → 前端：手势标签、置信度、各类别概率、推理耗时与累计 FPS。
4. 录制：把视频画面（含叠加层）保存为 MP4（当浏览器支持时）或 WebM。
5. 模型加载 / 卸载、摄像头设备选择、录制文件列表。

## 🗂️ 支持的手势（共 8 类，可通过 `load_model` 传入自定义标签）

| 索引 | 英文标签 | 中文含义 |
| :-- | :-- | :-- |
| 0 | `like` | 点赞 👍 |
| 1 | `fist` | 拳头 ✊ |
| 2 | `v_sign` | V字 ✌️ |
| 3 | `ok` | OK 👌 |
| 4 | `palm` | 掌心 🖐️ |
| 5 | `digit_1` | 数字 1 ☝️ |
| 6 | `digit_2` | 数字 2 ✌️ |
| 7 | `digit_5` | 数字 5 🖐️ |

> 如果希望识别「数字 3 / 数字 4」等更多类别，只需在调用 `load_model` 时传入自定义 `labels`。

## 🧰 目录结构

```
33/
├── package.json
├── vite.config.ts
├── tsconfig.json
├── index.html
├── src/
│   ├── main.tsx
│   ├── App.tsx
│   ├── labels.ts
│   └── styles.css
└── src-tauri/
    ├── Cargo.toml
    ├── tauri.conf.json
    ├── capabilities/
    │   └── default.json
    ├── build.rs
    ├── resources/
    │   └── model.onnx         <-- 放置你的 ONNX 模型文件
    └── src/
        ├── main.rs
        ├── state.rs
        ├── error.rs
        ├── inference.rs
        ├── server.rs
        ├── recording.rs
        └── frame.rs
```

## 🚀 运行

### 前置条件

1. Rust 工具链（`rustup`）
2. Node.js ≥ 18
3. ONNX Runtime 共享库：
   - **Windows**：从 [ONNX Runtime Releases](https://github.com/microsoft/onnxruntime/releases) 下载 `onnxruntime-win-x64-gpu-*.zip` 或 CPU 版本，解压后：
     - 将 `lib/onnxruntime.dll` 与 `onnxruntime_providers_*.dll` 复制到 `src-tauri/` 下，或加入 `PATH`。
     - 设置环境变量 `ORT_STRATEGY=system` 或 `ORT_DYLIB_PATH=<绝对路径到 onnxruntime.dll>`，
       以便 `ort` crate 的 `load-dynamic` 特性在运行时动态加载。
4. 一份导出为 ONNX 的手势识别模型（输入形状建议 `1x3x224x224`，输出形状 `1x8`，logits 即可，
   后端会自动做 softmax）。将其命名为 `model.onnx` 并放入 `src-tauri/resources/` 目录。

> 训练数据可使用 [HaGRID](https://github.com/hukenovs/hagrid) 等开源手势数据集，
> 用 PyTorch 训练 MobileNetV3 / ShuffleNet 等轻量模型后通过 `torch.onnx.export` 导出即可。

### 安装 & 开发模式

```bash
npm install
npm run tauri:dev
```

首次运行会自动：

1. 启动 Vite（端口 1420）。
2. 编译 Rust 后端并启动桌面窗口。
3. Rust 后端内部异步启动 WebSocket 服务（随机端口），通过事件把端口推送给前端，
   前端自动连接并开始双向收发数据。

### 打包发布

```bash
npm run tauri:build
```

打包产物位于 `src-tauri/target/release/bundle/`。

## 🧠 模型要求

- **输入**：`NCHW`（`1x3xHxW`，默认 `224x224`，可从模型读取动态）。
- **预处理**：后端会自动对图像做 `resize → /255 → ImageNet 均值方差归一化 (mean=[0.485,0.456,0.406], std=[0.229,0.224,0.225])`。
- **输出**：长度为类别数的 logits 张量，后端应用 softmax 得到概率。
- **推理目标**：单帧 ≤ 50ms（CPU 上 MobileNetV3-Small 通常在 10–30ms）。

## 🔌 WebSocket 协议

- 连接地址：`ws://127.0.0.1:<port>`（端口由后端事件 `ws://ready` 通知，也可通过 `get_ws_port` 查询）。
- 前端 → 后端：
  - 二进制消息：JPEG 字节流（推荐）。
  - 文本 JSON：`{"type":"frame","data":"<base64 JPEG>","timestamp":...}` 或 `{"type":"ping"}`。
- 后端 → 前端（二进制 JSON）：
  - `{"type":"result", "label":"like", "label_index":0, "confidence":0.92, "probabilities":[...], "inference_ms":18.4, "total_ms":22.1, "timestamp":...}`
  - `{"type":"stats", "fps":12.3, "avg_inference_ms":17.8, "queue_depth":0}`
  - `{"type":"status", "model_loaded":true, "port":12345}`
  - `{"type":"error", "message":"..."}`

## 📹 录制

- 点击右侧「开始录制」后，使用浏览器 `MediaRecorder` 录制带叠加层的画面。
- 优先使用 `video/mp4`（支持时），否则回退到 `video/webm;codecs=vp9`。
- 停止后通过 Tauri 命令 `save_recording` 保存到应用数据目录下的 `recordings/`：
  - Windows: `%APPDATA%\com.example.gesture-recognizer\recordings\`
  - macOS: `~/Library/Application Support/com.example.gesture-recognizer/recordings/`
  - Linux: `~/.config/com.example.gesture-recognizer/recordings/`
- 左侧面板可查看已有录制文件列表及大小，并一键打开所在目录。

## 🛠️ Tauri 命令

| 命令 | 说明 |
| :-- | :-- |
| `load_model` | 加载 ONNX 模型（可选参数：`path`, `labels`, `num_threads`） |
| `unload_model` | 卸载当前模型 |
| `get_model_status` | 查询当前模型加载状态、输入形状、标签等 |
| `get_ws_port` | 读取 WebSocket 服务端口 |
| `save_recording` | `{ filename, data_base64 }` → 保存录制文件 |
| `list_recordings` | 返回已有录制列表 |

## 📦 关键依赖

- **Tauri 2.x** + React 18 + Vite 5 + TypeScript。
- **Rust 侧**：`ort` (ONNX Runtime 绑定)、`image`、`tokio`、`tokio-tungstenite`、`ndarray`、`parking_lot`、`tracing`。

## 📝 许可

示例代码以 MIT 许可证发布，可自由修改与分发。ONNX Runtime 遵循其自身的 MIT 许可。
