# WebSocket 压测工具 (ws_stress)

基于 Python asyncio + aiohttp 的分布式 WebSocket 并发压测 CLI 工具。

## 功能特性

- **5000+ 并发客户端**: 基于 asyncio 异步架构，单机支持数千并发连接
- **随机房间分配**: 100 个房间，每个客户端随机加入
- **随机消息**: 消息长度 20-200 字节，可配置发送间隔
- **延迟统计**: P50/P95/P99 延迟百分位，最小/最大/平均延迟
- **成功率统计**: 连接成功率、消息到达率
- **分布式压测**: 多节点通过 Redis 协调任务分配
- **JSON 报告**: 结构化 JSON 报告输出
- **HTML 图表**: 延迟分布直方图 + 吞吐量曲线 (Chart.js)
- **爬坡控制**: `--ramp-up` 控制爬坡时间，渐进式增加连接数

## 安装

```bash
pip install -r requirements.txt
```

依赖:
- `aiohttp` - 异步 WebSocket 客户端
- `redis` - Redis 客户端 (分布式模式)
- `psutil` - 系统资源监控

## 使用方法

### 单机模式

```bash
python -m ws_stress \
  --url ws://localhost:8080/ws \
  --clients 5000 \
  --rooms 100 \
  --ramp-up 120 \
  --duration 300 \
  --output-dir ./output
```

### 分布式模式

**主节点 (Master)**:

```bash
python -m ws_stress \
  --url ws://target-server:8080/ws \
  --clients 5000 \
  --ramp-up 120 \
  --duration 300 \
  --redis redis://redis-host:6379/0 \
  --role master \
  --nodes 3 \
  --output-dir ./output
```

**从节点 (Slave)**:

```bash
python -m ws_stress \
  --url ws://target-server:8080/ws \
  --redis redis://redis-host:6379/0 \
  --role slave \
  --node-id node-2
```

### 参数说明

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--url` | str | 必填 | WebSocket 服务器地址 |
| `--clients` | int | 5000 | 总并发客户端数 |
| `--rooms` | int | 100 | 房间总数 |
| `--duration` | int | 300 | 压测持续时间 (秒) |
| `--ramp-up` | int | 120 | 爬坡时间 (秒), 0=无爬坡 |
| `--msg-min` | int | 20 | 消息最小长度 (字节) |
| `--msg-max` | int | 200 | 消息最大长度 (字节) |
| `--msg-interval` | float | 1.0 | 消息发送间隔 (秒) |
| `--room-prefix` | str | room | 房间名前缀 |
| `--output-dir` | str | ./output | 报告输出目录 |
| `--redis` | str | - | Redis 地址 (分布式) |
| `--role` | str | single | single/master/slave |
| `--nodes` | int | 1 | 节点总数 (master) |
| `--node-id` | str | 自动 | 节点 ID |

## WebSocket 服务器协议

工具期望服务器支持以下 JSON 消息格式:

**加入房间 (客户端 → 服务器)**:
```json
{"type": "join", "room": "room-1", "client_id": "client-0"}
```

**发送消息 (客户端 → 服务器)**:
```json
{"type": "message", "room": "room-1", "client_id": "client-0", "msg_id": "a1b2c3d4", "content": "..."}
```

**广播消息 (服务器 → 客户端)**:
```json
{"type": "broadcast", "room": "room-1", "client_id": "client-0", "msg_id": "a1b2c3d4", "content": "..."}
```

> 注意: 服务器收到消息后需广播回房间内所有客户端（包括发送者），工具通过 `msg_id` 匹配发送和接收时间来计算延迟。

## 输出说明

### JSON 报告 (`output/report_*.json`)

```json
{
  "node_id": "master-xxxxxx",
  "connection": {
    "success_rate": 99.8,
    "total": 5000,
    "successful": 4990,
    "failed": 10
  },
  "message": {
    "arrival_rate": 98.5,
    "total_sent": 150000,
    "total_received": 147750
  },
  "latency_ms": {
    "p50": 15.2,
    "p95": 45.8,
    "p99": 120.3
  },
  "throughput_curve": [...],
  "latency_distribution": [...]
}
```

### HTML 报告 (`output/report_*.html`)

- 延迟分布直方图 (7 个区间)
- 吞吐量曲线 (每秒消息数)
- 关键指标卡片

## 项目结构

```
ws_stress/
├── __init__.py          # 包入口
├── __main__.py          # python -m ws_stress 入口
├── cli.py               # CLI 参数解析 + 主调度
├── client.py            # WebSocket 客户端实现
├── config.py            # 数据结构定义
├── stats.py             # 统计收集与百分位计算
├── coordinator.py       # Redis 分布式协调
├── report.py            # JSON 报告生成
└── charts.py            # HTML 图表生成
```

## 注意事项

- 服务器的文件描述符限制 (`ulimit -n`) 需要足够大以支持大量并发连接
- 分布式模式下，各节点需要能访问同一 Redis 实例
- 建议在 Linux 系统上运行，Windows 下 asyncio 性能受限
- HTML 报告中的 Chart.js 通过 CDN 加载，需要联网环境查看
