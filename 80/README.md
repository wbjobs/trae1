# RTP 音视频质量监控系统

被动监听网络接口上的 RTP 流，实时解析并计算质量指标（MOS、丢包率、抖动、延迟、帧率、码率、分辨率），通过 PyQt 桌面应用和 Web Dashboard（Vue3 + ECharts）进行可视化展示。

**支持 SRTP 加密流解密**：通过预先配置的密钥或 Wireshark 格式的 keylog 文件，自动解密 SRTP 流，验证认证标签，解析加密的 RTP 头字段。

**AI 音视频内容分析**：每 5 秒对解码后的音频（PCM）和视频帧进行智能质量评估，自动检测声音卡顿、爆破音、回声、花屏、绿屏、画面冻结、马赛克等内容异常，超过阈值时标记为"内容异常"并自动保存异常片段（前后各 1 秒）供人工复核，AI 分析结果自动关联 RTP 层指标进行根因定位。

## 功能特性

- **被动 RTP 监听**：UDP 端口范围 5000-6000（可配置），无需修改现有 RTP 流配置
- **SRTP 解密支持**：
  - RFC 3711 标准 SRTP 协议（AES-128/256-CTR 加密 + HMAC-SHA1 认证
  - 密码套件：AES_CM_128_HMAC_SHA1_80（WebRTC 默认）、AES_CM_128_HMAC_SHA1_32、AES_CM_256_HMAC_SHA1_80/32
  - 头部加密（SRE）支持
  - ROC（回滚计数器）处理
  - 64 位滑动窗口重放保护
  - 认证标签验证（80/32 位截断）
- **DTLS-SRTP 密钥提取**：支持从 DTLS 握手包中提取 master_secret，通过 RFC 5764 TLS Exporter 派生 SRTP 密钥
- **Wireshark keylog 支持**：导入 SSLKEYLOG / SRTP_MASTER_KEY / SRTP 通用格式
- **RTP 头解析**：解析 Sequence、Timestamp、SSRC、Marker、Payload Type 等字段
- **实时质量指标**：
  - MOS-LQ（E-Model 简化算法，1-5 分）
  - 丢包率（基于 RTP Sequence gap）
  - 抖动（RFC 3550 Jitter 算法）
  - 延迟（基于 arrival_time 与 RTP timestamp 的相对差值）
- **视频流专用指标**：帧率、码率、分辨率（H.264 SPS 解析）
- **时间窗口**：每条流维护 10 秒滚动窗口的质量快照
- **AI 内容分析**（可开关）：
  - 每 5 秒自动分析解码后的音视频内容
  - 音频异常检测：卡顿（stutter）、爆破音（pop）、回声（echo）
  - 视频异常检测：花屏（glitch）、绿屏（green_screen）、画面冻结（freeze）、马赛克（mosaic）
  - 异常分数 0-100，超过阈值自动标记"内容异常"
  - 自动保存异常片段（前后各 1 秒），MP4 视频 + WAV 音频 + JSON 元数据
  - 根因分析：AI 结果与 RTP 丢包率、抖动、MOS 关联，判断是网络问题还是内容本身问题
- **Web Dashboard**：Vue3 + ECharts，实时推送所有流的 MOS 曲线、丢包/抖动、帧率/码率、延迟趋势、AI 分析结果
- **质量告警**：
  - MOS < 3 时自动保存视频截图（JPEG）或音频片段（WAV）
  - AI 内容异常时自动保存 2 秒异常片段（前后各 1 秒）
- **PDF 报表导出**：汇总表 + 趋势图 + 流详情 + AI 分析结果
- **桌面客户端**：PyQt5 GUI，支持启停监听、流列表、指标详情、截图/录音查看、SRTP 密钥管理、AI 分析详情

## 项目结构

```
e:\trae1\80\
├── main.py                    # 入口文件（支持 --keylog、--port-start、--port-end、--no-gui）
├── requirements.txt         # Python 依赖（新增 cryptography）
├── example_keylog.txt     # SRTP 密钥日志文件示例
├── backend/
│   ├── __init__.py
│   ├── rtp_parser.py      # RTP 报文解析
│   ├── rtp_listener.py    # UDP 端口监听（集成 SRTP 解密）
│   ├── srtp.py            # SRTP 核心：AES-CTR 解密 + HMAC-SHA1 认证 + 密钥派生
│   ├── keylog.py         # Wireshark keylog 文件解析器
│   ├── dtls_srtp.py      # DTLS-SRTP 密钥提取（TLS Exporter RFC 5764）
│   ├── quality.py        # 质量指标计算
│   ├── window.py         # 流注册表与时间窗口
│   ├── capture.py        # 截图与录音模块
│   ├── report.py         # PDF 报表导出
│   ├── web_api.py        # aiohttp REST + WebSocket
│   └── app.py            # PyQt 主窗口（新增 SRTP Keys Tab）
├── web/
│   └── static/
│       └── index.html         # Vue3 + ECharts Dashboard
├── frontend/                  # 可选的 npm 工程脚手架
│   └── package.json
├── screenshots/            # 自动保存的视频截图
├── recordings/          # 自动保存的音频片段
└── reports/             # 导出的 PDF 报表
```

## 安装与运行

### 1. 安装依赖

```bash
pip install -r requirements.txt
```

> **注意**：`aiortc` 在 Windows 上需要安装 Microsoft Visual C++ Redistributable。
> 如果仅使用 RTP 被动监听（而非 WebRTC 建连），`aiortc` 依赖可按需调整。
> `cryptography` 用于 SRTP 加解密和 HMAC 认证。

### 2. 启动

#### GUI 模式（默认）

```bash
# 正常启动
python main.py

# 启动时自动导入 keylog 文件
python main.py --keylog example_keylog.txt

# 自定义端口范围和 MOS 阈值
python main.py --keylog keys.log --port-start 10000 --port-end 20000 --mos-threshold 3.5
```

#### Headless 模式（无 GUI，仅 Web Dashboard）

```bash
python main.py --no-gui --keylog example_keylog.txt
```

### 3. 打开 Web Dashboard

启动监听后，在浏览器中访问：

```
http://localhost:8080
```

Web Dashboard 会通过 WebSocket 实时接收质量指标推送，无需刷新页面。

## SRTP 解密使用指南

### 方法一：命令行参数导入 keylog 文件

```bash
python main.py --keylog /path/to/keylog.txt
```

### 方法二：GUI 中导入 keylog 文件

1. 启动应用后切换到 **"SRTP Keys" Tab
2. 点击 **"Import Keylog File"** 按钮
3. 选择 keylog 文件
4. 系统会自动解析并导入所有 SRTP 密钥

### 方法三：手动添加 SRTP 密钥

1. 在 "SRTP Keys" Tab 中点击 **"Add Key Manually"** 按钮
2. 填写：
   - SSRC（十六进制或十进制）
   - 角色（from-client / from-server / unknown）
   - 密码套件（默认 AES_CM_128_HMAC_SHA1_80）
   - Master Key（十六进制）
   - Master Salt（十六进制）
   - 是否启用头部加密（Header Encryption / SRE）
3. 点击 **"Add"** 完成添加

### 方法四：DTLS-SRTP 密钥派生

如果已经通过 Wireshark 或其他工具获取了 DTLS 握手的 master_secret 和 random 值：

1. 在 **"DTLS-SRTP Key Derivation"** 区域填写：
   - SSRC
   - Master Secret（48 字节 hex）
   - Client Random（32 字节 hex）
   - Server Random（32 字节 hex）
   - SRTP 套件
   - 角色
2. 点击 **"Derive & Add SRTP Key"** 自动派生并添加密钥

### Keylog 文件格式

支持以下格式：

#### 1. SRTP_MASTER_KEY（Wireshark 原生）

```
SRTP_MASTER_KEY "from-client" 0x12345678 aabbccddeeff00112233445566778899 aabbccddeeff00112233445566
```

#### 2. SRTP 通用格式

```
SRTP from-client 0x12345678 aabbccddeeff00112233445566778899 aabbccddeeff00112233445566 AES_CM_128_HMAC_SHA1_80 header_enc
```

#### 3. CLIENT_RANDOM（TLS/DTLS 密钥日志）

```
CLIENT_RANDOM <32字节hex> <48字节hex>
```

#### 4. RFC5705_KEYING_MATERIAL_EXPORTER

```
RFC5705_KEYING_MATERIAL_EXPORTER EXTRACTOR-dtls_srtp <client_random> <server_random> <exporter_data>
```

### SRTP 解密流程

```
SRTP 包接收
    ↓
提取明文 SSRC（RFC 3711 规定 SSRC 不加密）
    ↓
查找 SrtpManager 中是否有该 SSRC 的上下文
    ↓ 有密钥 ↓ 无密钥
解密 + 认证验证 → 丢弃，按明文 RTP 处理
    ↓
验证通过？
    ↓ 是 ↓ 否
解密 RTP 头和负载 → 丢弃，计数 auth_failed
    ↓
重放保护检查？
    ↓ 是 ↓ 否
更新 ROC（序列号回绕处理） → 丢弃，计数 replay_dropped
    ↓
明文 RTP 交给解析器
```

### SRTP 状态监控

- **流列表**新增 "SRTP" 列，显示每个流是否加密状态（Y/N + 角色）
- **SRTP Keys Tab** 显示：
  - 所有已配置密钥的 SSRC、角色、套件
  - 已解密包数
  - 认证失败数
  - 重放丢弃数
  - 是否启用头部加密
- **流详情**显示 SRTP 套件和解密统计

## 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--keylog FILE` | Wireshark 格式 keylog 文件路径 | None |
| `--port-start N` | UDP 端口范围起始 | 5000 |
| `--port-end N` | UDP 端口范围结束 | 6000 |
| `--mos-threshold FLOAT | MOS 告警阈值 | 3.0 |
| `--no-gui` | Headless 模式（无 GUI） | False |

## Web Dashboard API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/streams` | 获取所有活跃流的摘要信息 |
| GET  | `/api/streams/{ssrc}` | 获取指定流的 10 秒窗口指标 |
| POST | `/api/report` | 导出 PDF 质量报表 |
| GET  | `/ws` | WebSocket 实时推送（每秒推送） |
| GET  | `/` | Dashboard 主页 |

WebSocket 推送消息格式：

```json
{
  "type": "metrics",
  "timestamp": 1716652800.0,
  "streams": [
    {
      "ssrc": "0x12345678",
      "ssrc_int": 305419896,
      "media_kind": "video",
      "codec": "H264",
      "latest": {
        "timestamp": 1716652800.0,
        "mos": 4.2,
        "loss_rate": 0.01,
        "jitter": 5.3,
        "delay": 120.5,
        "fps": 30.0,
        "bitrate": 2500.0,
        "width": 1920,
        "height": 1080
      }
    }
  ]
}
```

## MOS 计算说明

采用简化版 E-Model：

```
R = 94.2 - Ie - Id - Ij
  Ie = 丢包率 × 190        // 丢包影响
  Id = max(0, (延迟-150) × 0.02)  // 延迟影响 (>150ms 时)
  Ij = 抖动 × 0.1           // 抖动影响

MOS = 1 + 0.035R + 7e-6 × R × (R-60) × (100-R)
```

MOS 分级：

| 范围 | 等级 | 说明 |
|------|------|------|
| 4.0 - 4.5 | Excellent | 优秀 |
| 3.5 - 3.9 | Good | 良好 |
| 3.0 - 3.4 | Fair | 一般 |
| 2.0 - 2.9 | Poor | 较差（告警） |
| 1.0 - 1.9 | Bad | 很差（告警） |

## 支持的密码套件

| 套件 | 密钥长度 | Salt 长度 | 认证标签 |
|------|----------|----------|----------|
| AES_CM_128_HMAC_SHA1_80 | 128 位 | 112 位 | 80 位 |
| AES_CM_128_HMAC_SHA1_32 | 128 位 | 112 位 | 32 位 |
| AES_CM_256_HMAC_SHA1_80 | 256 位 | 112 位 | 80 位 |
| AES_CM_256_HMAC_SHA1_32 | 256 位 | 112 位 | 32 位 |

## 注意事项

- 被动监听模式下，需要确保运行机器的网络接口能够接收到目标 RTP 流（端口镜像、SPAN 或同一网段）
- **SRTP 解密**：
  - SSRC 始终为明文（RFC 3711），用于查找对应密钥上下文
  - 序列号回绕（ROC）自动处理
  - 头部加密（SRE）需要显式配置 `header_enc` 标志
  - DTLS-SRTP 需要从浏览器或其他 WebRTC 应用导出 master_secret
  - 建议使用 Chrome 的 `--ssl-key-log-file` 参数生成的 keylog 文件配合使用
- **AI 内容分析**：
  - 本地轻量实现，无需外部 API 或大模型依赖，使用信号处理和图像处理算法
  - 视频解码依赖 PyAV（可选），未安装时降级使用 G.711 音频解码
  - 内容缓冲默认保留 3 秒数据，确保异常时能截取前后各 1 秒
  - 异常片段保存为 MP4/H.264（需要 PyAV）或原始 RGB 帧，音频保存为 WAV
  - 根因分析基于启发式规则，关联 RTP 层丢包率、抖动和 MOS 指标
- 分辨率解析仅支持 H.264 SPS，其他编解码器（H.265、VP8、VP9、AV1）暂不支持自动探测
- 音频录音默认假设 G.711（PT=0/8）或 G.722（PT=9），其他编解码器保存为原始数据
- RTP 时钟频率默认使用 90kHz，实际延迟计算可能存在偏差
- 运行时动态添加的密钥会立即生效，无需重启监听
