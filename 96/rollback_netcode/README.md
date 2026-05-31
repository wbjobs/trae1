# Rollback Netcode - Input Prediction & Client Compensation

一个完整的回滚网络代码（Rollback Netcode）实现，用于游戏网络编程中的输入预测和客户端补偿。

## 功能特性

### 1. 输入预测 (Input Prediction)
- 客户端在用户输入时立即更新本地角色位置，无需等待服务器响应
- 同时将输入发送到服务器进行权威计算
- 预测窗口可配置（默认100ms）

### 2. 确定性物理引擎 (Deterministic Physics)
- **固定时间步长** (Fixed Timestep): 1/60秒
- **确定性随机数生成器**: 可重复的随机序列
- **AABB碰撞检测**: 精确的墙壁和玩家碰撞响应
- **状态哈希验证**: 确保客户端和服务器状态一致

### 3. 回滚机制 (Rollback Mechanism)
- 服务器计算权威状态后发送校正包
- 检测预测错误，超过阈值触发回滚
- 回滚到错误发生的帧，重放所有未确认的输入
- 最多回滚帧数可配置（默认10帧）

### 4. 平滑插值修正 (Smooth Correction)
- 小误差（<2像素）: 使用线性插值平滑过渡
- 大误差（>2像素）: 立即回滚确保正确性
- 平滑系数可配置（默认0.1）
- 其他玩家位置使用插值平滑显示

### 5. 调试可视化 (Debug Visualization)
- 红色填充圆: 预测位置
- 白色轮廓圆: 权威位置
- 黄色连线: 预测误差
- 蓝色轨迹: 移动历史
- 回滚闪光动画: 回滚发生时的视觉反馈
- 详细的调试面板: 显示ping、帧号、回滚统计等

## 架构

```
┌─────────────────┐        ┌─────────────────┐
│   Game Client   │        │   Game Server   │
│                 │        │                 │
│  Input Pred.    │───────▶│  Auth. Physics │
│  Rollback Mgr   │◀───────│  Corrections   │
│  Interpolation  │        │                 │
└─────────────────┘        └─────────────────┘
          │                           │
          ▼                           ▼
┌─────────────────┐        ┌─────────────────┐
│ Debug Visualizer│        │ Deterministic   │
│ (Pygame)        │        │ Physics Engine  │
└─────────────────┘        └─────────────────┘
```

## 工作原理

### 正常流程
```
Client Input → Local Prediction (立即移动) → Send to Server
                                                ↓
                                       Server Physics (权威计算)
                                                ↓
Client Correction ← State Update + Correction
         ↓
 (如果预测正确) → 继续预测
 (如果预测错误) → 回滚 + 重放
```

### 回滚流程
```
1. 客户端在帧100预测移动到位置A
2. 服务器在帧100计算实际位置为B
3. 服务器发送校正包 (位置B, 帧100)
4. 客户端检测到A≠B，距离>阈值
5. 客户端回滚到帧100，设置位置为B
6. 客户端重放帧101-105的所有未确认输入
7. 客户端现在与服务器状态一致
```

## 安装

```bash
# 安装依赖
pip install -r requirements.txt

# 或手动安装
pip install pygame
```

## 快速开始

### 查看演示说明
```bash
python main.py demo
```

### 启动服务端
```bash
# 默认配置
python main.py server

# 自定义配置
python main.py server --port 5555 --tick-rate 60 --prediction-window 100
```

### 启动客户端
```bash
# 带可视化界面（需要pygame）
python main.py client --host 127.0.0.1

# 无头模式（文本输出）
python main.py client --headless

# 自定义预测窗口
python main.py client --prediction-window 150 --smoothing 0.15
```

## 控制

| 按键 | 功能 |
|------|------|
| W / ↑ | 向上移动 |
| S / ↓ | 向下移动 |
| A / ← | 向左移动 |
| D / → | 向右移动 |
| Space | 动作键 |

## 命令行参数

### 服务端参数
| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 0.0.0.0 | 绑定地址 |
| `--port` | 5555 | 监听端口 |
| `--tick-rate` | 60 | 服务器tick率 (Hz) |
| `--timestep` | 0.01667 | 固定时间步长 (秒) |
| `--prediction-window` | 100 | 预测窗口 (ms) |
| `--max-rollback` | 10 | 最大回滚帧数 |
| `--player-speed` | 200 | 玩家移动速度 |
| `--player-size` | 30 | 玩家大小 |
| `--world-width` | 800 | 世界宽度 |
| `--world-height` | 600 | 世界高度 |
| `--log-level` | INFO | 日志级别 |

### 客户端参数
| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 127.0.0.1 | 服务器地址 |
| `--port` | 5555 | 服务器端口 |
| `--tick-rate` | 60 | 客户端tick率 (Hz) |
| `--timestep` | 0.01667 | 固定时间步长 (秒) |
| `--prediction-window` | 100 | 预测窗口 (ms) |
| `--max-rollback` | 10 | 最大回滚帧数 |
| `--smoothing` | 0.1 | 校正平滑系数 |
| `--headless` | False | 无头模式（无pygame） |
| `--log-level` | INFO | 日志级别 |

## 项目结构

```
rollback_netcode/
├── config.py          # 配置类
├── entity.py          # 实体类（Vector2、PlayerState、GameWorld等）
├── physics.py         # 确定性物理引擎
├── rollback.py        # 回滚管理器核心逻辑
├── network.py         # 网络协议和连接管理
├── server.py          # 游戏服务器
├── client.py          # 游戏客户端（输入预测、回滚）
├── visualizer.py      # 调试可视化（pygame）
├── main.py            # CLI入口
├── requirements.txt   # 依赖列表
└── README.md          # 本文件
```

## 核心模块说明

### DeterministicPhysics (`physics.py`)
```python
class DeterministicPhysics:
    def step(world, inputs, dt)          # 单步物理更新
    def simulate_frames(world, inputs)   # 模拟多帧（用于回滚）
    def predict_player_position(...)     # 预测玩家位置
    def compare_states(world1, world2)   # 比较状态差异
    def get_state_hash(world)            # 计算状态哈希
```

### RollbackManager (`rollback.py`)
```python
class RollbackManager:
    def add_input(player_id, input)       # 添加输入到缓冲区
    def save_state(world)                 # 保存游戏状态
    def rollback_to_frame(frame)          # 回滚到指定帧
    def resimulate_frames(...)            # 重模拟帧
    def apply_correction(...)             # 应用校正
    def check_prediction_error(...)       # 检查预测错误
    def get_statistics()                  # 获取统计信息
```

### GameClient (`client.py`)
```python
class GameClient:
    def connect()                         # 连接服务器
    def set_input(...)                    # 设置当前输入
    def get_local_player()                # 获取本地玩家状态
    def get_world_state()                 # 获取世界状态
    def get_debug_info()                  # 获取调试信息
```

### GameServer (`server.py`)
```python
class GameServer:
    def start()                           # 启动服务器
    def stop()                            # 停止服务器
    def get_world()                       # 获取世界状态
```

## 调试面板说明

启动客户端后，窗口底部显示调试面板：

```
┌─────────────────────────────────────────────────────────────────────┐
│ PING: 50ms | FRAME: 1234 | PREDICTED_AHEAD: 1 frames              │
│ ROLLBACKS: 5 | AVG_ROLLBACK: 2.3 frames | LAST_ROLLBACK: 1200    │
│ CORRECTIONS: 120 | SMOOTH_CORR: 110 | PRED_ERRORS: 15            │
│ STATE_HISTORY: 20 | CONFIRMED_FRAME: 1233 | CONNECTED: YES       │
├─────────────────────────────────────────────────────────────────────┤
│  [●] Filled Circle: Predicted Position                              │
│  [○] Outline Circle: Authoritative Position                         │
│  [─] Yellow Line: Prediction Error                                  │
│  [─] Blue Trail: Movement History                                   │
└─────────────────────────────────────────────────────────────────────┘
```

## 回滚触发条件

回滚在以下情况触发：

1. **位置误差 > 2像素** - 预测位置与权威位置差距过大
2. **碰撞检测错误** - 客户端预测穿墙，但服务器检测到碰撞
3. **玩家碰撞** - 与其他玩家的碰撞预测错误
4. **边界超出** - 预测位置超出世界边界

## 性能优化

1. **状态历史管理** - 自动清理过旧的状态，限制历史长度
2. **输入缓冲区** - 环形缓冲区存储输入，避免内存泄漏
3. **增量校正** - 只发送校正数据，不发送完整状态
4. **帧同步** - 服务器和客户端使用相同的时间步长

## 扩展功能

可以扩展的功能：

- [ ] 延迟模拟（人工添加网络延迟和丢包）
- [ ] 数据包压缩
- [ ] 断线重连
- [ ] 回放系统（记录和重放游戏）
- [ ] 更多玩家（当前最多4人）
- [ ] 游戏规则和得分系统
- [ ] WebGL版本（使用Pygame JS或其他引擎）

## 技术细节

### 确定性保证
- 使用固定种子的随机数生成器
- 所有计算使用浮点数，避免精度差异
- 状态哈希用于验证一致性

### 网络协议
- TCP协议（可靠传输）
- JSON格式的数据包
- 每个包包含类型、帧号、时间戳
- 心跳检测连接状态

### 时间同步
- 服务器和客户端使用相同的固定时间步长
- 客户端预测最多超前100ms（6帧）
- 服务器每帧广播权威状态

## License

MIT License - 仅供学习和研究使用。
