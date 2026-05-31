# 端侧语音命令词识别桌面应用

基于 Tauri + Rust + ONNX Runtime 的端侧语音命令词识别系统，支持实时音频采集、本地推理和智能家居控制。

## 功能特性

### 核心功能
- **实时音频采集**: 16kHz 单声道麦克风实时采集，支持环形缓冲区
- **语音预处理**: Rust 端实现 MFCC 特征提取（梅尔频率倒谱系数）
- **命令词识别**: ONNX Runtime 推理，支持 10 个中文命令词
- **热词唤醒**: "你好小智" 唤醒词检测，唤醒后 10 秒内可识别命令
- **智能家居集成**: WebSocket 实时发送识别结果到智能家居服务
- **性能优化**: 每帧推理时间 < 100ms，完全端侧运行

### 识别命令词
| 序号 | 命令词 | 智能家居动作 |
|-----|--------|-------------|
| 1 | 开机 | power_on |
| 2 | 关机 | power_off |
| 3 | 调高音量 | volume_up |
| 4 | 调低音量 | volume_down |
| 5 | 下一首 | next_track |
| 6 | 上一首 | previous_track |
| 7 | 暂停 | pause |
| 8 | 播放 | play |
| 9 | 静音 | mute |
| 10 | 取消静音 | unmute |

### UI 功能
- 实时识别结果显示（置信度条 + 命令词）
- 10 个命令词置信度柱状图
- 唤醒状态指示
- 识别历史记录（最多 200 条）
- 可调节的置信度阈值和热词阈值
- 模拟测试功能（方便调试）

## 技术架构

### Rust 后端
```
src/
├── audio/              # 音频模块
│   ├── capture.rs      # 麦克风采集 (cpal)
│   ├── mfcc.rs         # MFCC 特征提取
│   └── mod.rs
├── inference/          # 推理模块
│   ├── model.rs        # ONNX 命令词模型
│   ├── hotword.rs      # 热词检测 (DTW 动态时间规整)
│   └── mod.rs
├── websocket/          # 网络模块
│   ├── client.rs       # WebSocket 客户端
│   └── mod.rs
├── state/              # 状态管理
│   ├── app_state.rs    # 应用状态
│   └── mod.rs
└── main.rs             # Tauri 命令桥接
```

### 前端界面
```
src/
├── index.html          # 主页面
├── style.css           # 样式
├── main.js             # 业务逻辑
└── tauri.js            # Tauri API 兼容层
```

## 构建和运行

### 环境要求
- Rust 1.70+
- Node.js 18+
- ONNX Runtime (自动通过 ort crate 加载)

### 依赖安装
```bash
# 安装 Node 依赖
npm install

# Rust 依赖会在构建时自动安装
```

### 准备模型
1. 将预训练的 ONNX 模型放在 `models/command_model.onnx`
2. 模型要求：
   - 输入形状: `[1, 98, 13, 1]` (batch, time_frames, mfcc_coeffs, channels)
   - 输出形状: `[1, 10]` (10 个命令词 logits)
   - 98 帧 = 约 1 秒音频 (25ms 帧长 + 10ms 帧移)

### 开发运行
```bash
# 开发模式
npm run dev

# 或者
npx tauri dev
```

### 构建发布
```bash
# Windows 安装包
npm run build

# 或者
npx tauri build
```

## 测试

### 模拟智能家居服务
使用 Python 快速启动一个 WebSocket 测试服务器：

```python
import asyncio
import websockets
import json

async def handler(websocket):
    print("智能家居服务已启动，等待连接...")
    async for message in websocket:
        data = json.loads(message)
        print(f"收到命令: {data['label']} (置信度: {data['confidence']:.2f})")
        
        response = {
            "success": True,
            "action": data["action"],
            "message": f"已执行: {data['label']}",
            "timestamp": data["timestamp"]
        }
        await websocket.send(json.dumps(response))

async def main():
    async with websockets.serve(handler, "localhost", 8765):
        await asyncio.Future()

asyncio.run(main())
```

### 模拟命令测试
在应用的"设置"面板中，可以使用"模拟测试"功能发送测试命令，无需实际语音输入。

## 配置说明

### 音频参数
- 采样率: 16000 Hz
- 声道: 单声道 (Mono)
- 采样格式: f32
- 帧长: 1000ms
- 帧移: 300ms (重叠)
- 热词窗口: 1500ms

### MFCC 参数
- 系数数量: 13
- Mel 滤波器组: 40
- FFT 大小: 512
- 预加重: 0.97
- 最低频率: 20Hz
- 最高频率: 8000Hz

## 性能指标

- 音频采集延迟: < 10ms
- MFCC 特征提取: < 10ms/秒音频
- ONNX 推理: < 50ms (取决于模型)
- 总处理延迟: < 100ms
- 内存占用: ~100MB

## 目录结构

```
voice-command/
├── src/                    # 前端代码
├── src-tauri/              # Rust 后端
│   ├── src/                # Rust 源码
│   ├── Cargo.toml          # Rust 依赖
│   ├── tauri.conf.json     # Tauri 配置
│   ├── icons/              # 应用图标
│   └── capabilities/       # 权限配置
├── models/                 # ONNX 模型目录
├── package.json            # Node 配置
└── .gitignore
```

## 常见问题

### Q: 模型加载失败怎么办？
A: 检查模型路径是否正确，确保 ONNX 文件格式正确。如果没有模型，可以使用"模拟测试"功能体验 UI。

### Q: 麦克风没有声音？
A: 检查系统麦克风权限，确保应用有权访问麦克风。

### Q: WebSocket 连接失败？
A: 确保智能家居服务已启动，地址和端口配置正确。

## License

MIT
