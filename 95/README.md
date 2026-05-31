# Cloud Gamepad · WebTransport

一个云游戏手柄输入系统：浏览器读取 Gamepad API，通过 **WebTransport Datagram**（不可靠但低延迟）发送到云端游戏服务器，在服务器侧通过 **uinput (Linux)** 或 **ViGEmBus (Windows)** 模拟成虚拟手柄设备。

## 特性

- 🎮 最多 **4 个手柄** 同时连接到同一游戏实例
- ⚡ WebTransport Datagram 不可靠低延迟通道，目标 P95 < 30ms
- 🎛 摇杆 XY、按键按下/弹起、扳机力度全量采集
- 🔁 **按键映射**：Xbox / PS5 / Switch Pro 布局可切换
- 📉 **丢包处理**：线性插值根据前后帧估算丢失的输入
- 🔀 **乱序重组**：16 位序列号 + 滑动窗口（默认 64）+ 20ms 超时，解决倒走/瞬移
- 🔄 **Rollback Netcode**：客户端预测 + 服务端权威 + 平滑回滚，零延迟输入感
- 🎯 **调试可视化**：实时显示预测位置 vs 权威位置、校正差异、回滚统计
- 🧪 **延迟测试面板**：RTT / P95 / 丢包率 / 乱序事件 / 已发送-已确认实时图表
- 🖥 双后端虚拟设备：Linux uinput、Windows ViGEmBus，其他平台自动降级到 mock
- 🎲 **确定性物理引擎**：fixed timestep + 确定性随机数，前后端模拟一致

## 目录结构

```
backend/
  cmd/server/           # Go 可执行入口
  internal/
    protocol/           # Datagram 协议编解码
    server/             # WebTransport 服务器 + 会话管理
    vgamepad/           # 虚拟手柄：uinput / ViGEm / mock
    interpolator/       # 丢包插值器
    mapping/            # Xbox/PS5/Switch 布局映射
frontend/
  src/
    codec.ts            # 前端侧协议编解码
    transport.ts        # WebTransport 客户端 + RTT/丢包统计
    useGamepads.ts      # React hook：Gamepad API 轮询
    components/         # GamepadCard / LatencyPanel
    App.tsx             # 主界面
```

## 快速开始

### 后端

```bash
cd backend
go mod tidy
go run ./cmd/server -addr :4443 -max-pads 4
```

首次启动会在用户缓存目录自动生成自签名 TLS 证书。

### 前端

```bash
cd frontend
npm install
npm run dev
```

浏览器访问 <http://localhost:5173> ，点击「连接」即可。

> 浏览器要求 WebTransport 使用 HTTPS + 受信任证书（或 `localhost` 例外）。若连接非 `localhost` 的服务器，请将自签 CA 导入系统信任库。

## 协议 (Datagram)

| Byte | 字段 | 说明 |
|------|------|------|
| 0 | type | `0x01` 输入 / `0x02` ACK / `0x03` Ping / `0x04` Pong / `0x05` 映射 |
| 1… | uvarint seq | 客户端单调递增 |
| … | uvarint ts | `performance.now()` ms |
| … | uint8 padIndex | 0-3 |
| … | 3 bytes 按钮位图 | 17 位 |
| … | 6×float32 轴 | LX LY RX RY LT RT |

## 性能 & 丢包

- 发送频率：~125 Hz（每 8ms 一次）
- 通道：WebTransport Datagram（QUIC 不可靠 0-RTT）
- 丢包策略：服务端保存最近 4 帧，若 seq 非连续，用 `prev + (prev - prev_prev) * 0.5` 线性估算丢失输入
- 测量：前端通过 Ping/Pong 记录 RTT，滑动窗口 200 个样本计算 P95

## 乱序处理机制

云游戏场景中网络抖动可能导致输入包乱序到达，表现为游戏角色"倒走"或"瞬移"（后发的高序号输入先到并覆盖先到的低序号输入）。本系统通过以下机制解决：

1. **16 位序列号**：客户端为每个输入包分配递增的 16 位序列号（模 65536 循环）
2. **滑动窗口重组**：服务端使用大小为 N（默认 64）的环形缓冲，包到达后按序列号缓存
3. **顺序投递**：只有当 `nextExpected` 序号的包到达后，才按顺序连续递交给虚拟设备
4. **20ms 超时回退**：如果窗口内有缺失的包且超过 20ms 未到，则跳过缺失包，使用已收到的最新有效包继续
5. **乱序事件统计**：所有乱序事件记录到日志并上报到前端延迟面板（`reorderEvents` 指标）

```
  到达顺序:  1 → 3 → 4 → 2 → 6 → 5 ...
  滑动窗口: [1,_,_,_,_,_] → 投递 1
             [_,3,4,_,_,_] → 缓存等待 2
             [_,3,4,2,_,_] → 2 到达 → 投递 2,3,4
             [_,_,_,_,6,5] → 投递 5,6
```

**配置调优：**
- 弱网环境可增大 `-reorder-buffer`（最大 1024），但会增加延迟
- 竞技场景可减小 `-reorder-timeout`（如 10ms），优先保证低延迟

## Rollback Netcode (客户端预测 + 回滚)

云游戏最大的体验痛点是输入延迟——按下按键要等 RTT/2 才看到响应。Rollback Netcode 通过**客户端立即预测** + **服务端权威修正** + **平滑回滚**来实现零延迟输入感：

### 核心机制

```
┌─────────────────────────────────────────────────────────────────┐
│ 客户端 (浏览器)                                                    │
│  ┌─────────┐   ┌───────────┐   ┌───────────┐   ┌─────────────┐  │
│  │ 手柄输入 │→ │  本地预测  │→ │  立即渲染  │→ │  发送到服务端 │  │
│  └─────────┘   └───────────┘   └───────────┘   └─────────────┘  │
│        ↑                                                  ↓     │
│        │  回滚重放                                    校正包     │
│        └──────────────────────────────────────────────────┘     │
│                             ▲                                    │
└─────────────────────────────┼────────────────────────────────────┘
                              │ 权威物理 60Hz
                          ┌───┴────┐
                          │ 服务端 │
                          └────────┘
```

### 关键技术

1. **确定性物理引擎** (`backend/internal/physics/`, `frontend/src/physics/`)
   - 前后端**完全相同**的 Go/TS 双实现
   - `FixedTimestep = 1/60` 固定步进，浮点运算顺序严格一致
   - `DeterministicRNG` 线性同余生成器，相同种子产生相同序列
   - 碰撞检测、摩擦、加速度计算完全一致

2. **客户端预测** (`frontend/src/rollback/client.ts`)
   - 浏览器本地同步运行相同的物理引擎
   - 手柄输入**立即**应用到本地预测，无需等待服务端响应
   - 预测窗口默认 100ms（约 6 帧），根据 RTT 可配
   - 自动记录输入历史用于回滚

3. **服务端权威** (`backend/internal/rollback/manager.go`)
   - 服务端运行唯一的权威物理实例，60Hz 步进
   - 收集所有玩家输入，计算真实物理状态
   - 与客户端上报的预测位置对比，差异 > 阈值（默认 0.5px）时发送校正包
   - 60Hz 广播全局状态同步

4. **回滚与平滑**
   - 收到校正包 → 回滚到校正帧 → 应用权威状态 → 重放所有未确认输入
   - 位置校正使用 `easeOutCubic` 插值 6 帧完成，避免跳帧
   - 预测错误（如穿墙）立即触发回滚

### 调试可视化

前端 `RollbackDebugger` 组件实时显示：
- 🔵 **蓝圆**：客户端预测位置（本地立即响应）
- 🟢 **绿圈**：服务端权威位置（真实物理位置）
- 🔴 **虚线**：两者之间的误差向量
- 实时显示：误差距离、回滚次数、平均校正量

### 前端配置

在主界面 "Rollback Netcode 配置" 面板中：
- **启用客户端预测**：一键开关，关闭后回到传统"等待服务端响应"模式
- **预测窗口**：20ms - 300ms 滑块调节，网络差时增大

## 依赖

- Go ≥ 1.22，`quic-go/webtransport-go`
- Windows 手柄模拟需要安装 [ViGEmBus](https://github.com/nefarius/ViGEmBus)
- Linux 手柄模拟需要 `/dev/uinput` 权限

## License

MIT
