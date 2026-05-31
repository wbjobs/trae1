# 温室环境模拟系统

基于 Go + Gin 后端、React + ECharts 前端的温室环境实时监控模拟系统。

## 架构

```
┌──────────────┐  1Hz  ┌──────────────┐  MQTT  ┌─────────────┐
│  模拟器(Sim)  │──────▶│  MQTT Broker │◀──────│  Go 后端     │
│ (昼夜变化)    │       │  (Mosquitto) │       │  - REST API  │
└──────────────┘       └──────────────┘       │  - WebSocket │
                                              │  - 预警检测  │
                                              └──────┬──────┘
                                                     │
                                              ┌──────▼──────┐
                                              │  InfluxDB   │
                                              │(sensors+alerts)│
                                              └─────────────┘
                                                     │
                                              ┌──────▼──────┐
                                              │ React 前端   │
                                              │  - 实时图表  │
                                              │  - 预警通知  │
                                              │  - 历史查询  │
                                              └─────────────┘
```

## 功能

- **5个温室区域**：番茄区、叶菜区、育苗区、草莓区、花卉区
- **4种传感器**：温度、湿度、光照强度、CO₂浓度
- **模拟算法**：基于正弦/余弦函数模拟昼夜变化规律，加入随机抖动
- **MQTT 实时推送**：每秒一次，主题 `greenhouse/{zone_id}/sensors`
- **InfluxDB 持久化**：时序数据存储，支持历史查询
- **REST API**：历史数据查询，支持多时间范围（5分钟/1小时/24小时）
- **WebSocket 实时推送**：后端消费 MQTT 消息后通过 WebSocket 推送到前端
- **消息去重**：前后端双重去重，防止 MQTT 重连导致数据重复

## 快速开始

### 1. 启动基础设施

```bash
docker-compose up -d
```

启动 InfluxDB 2.7 和 Mosquitto MQTT Broker。

### 2. 启动后端

```bash
cd backend
go mod tidy
go run main.go
```

配置环境变量（可选）：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `HTTP_ADDR` | `:8080` | HTTP 监听地址 |
| `MQTT_BROKER` | `tcp://127.0.0.1:1883` | MQTT Broker |
| `MQTT_USER` | - | MQTT 用户名 |
| `MQTT_PASS` | - | MQTT 密码 |
| `INFLUX_URL` | `http://127.0.0.1:8086` | InfluxDB URL |
| `INFLUX_ORG` | `greenhouse` | InfluxDB Org |
| `INFLUX_TOKEN` | `dev-token` | InfluxDB Token |
| `INFLUX_BUCKET` | `greenhouse` | InfluxDB Bucket |

### 3. 启动前端

```bash
cd frontend
npm install
npm run dev
```

访问 http://localhost:3000

## API 文档

### 获取区域列表

```
GET /api/zones
```

### 查询历史数据

```
GET /api/history?zone_id=zone-1&range=5m&metric=temperature
```

| 参数 | 必填 | 可选值 |
|------|------|--------|
| `zone_id` | 是 | `zone-1` ~ `zone-5` |
| `range` | 否 | `5m`, `1h`, `24h` |
| `metric` | 否 | `temperature`, `humidity`, `light`, `co2` |

### 实时 WebSocket

```
WS /api/realtime
```

推送 JSON 格式的 `SensorReading` 消息。

### 健康检查

```
GET /api/health
```

## 消息去重机制

### 问题

MQTT 连接断开重连后，积压消息被一次性推送，导致前端图表时间轴上数据重叠。

### 解决方案（前后端双重去重）

**后端**（`backend/api/server.go`）：

- MQTT 消费端使用 `dedupMap` 基于 `zone_id + timestamp` 组合键去重
- 300 秒滑动窗口，自动清理过期条目
- 重复消息直接丢弃，不广播到 WebSocket

**前端**（`frontend/src/App.jsx`）：

- `recentTimestampsRef` 维护已处理的 `(zoneId, timestamp)` 集合
- 窗口大小 300 秒，自动淘汰过期条目
- 收到重复消息时打印日志并跳过，不触发图表更新

## 目录结构

```
.
├── backend/
│   ├── main.go           # 入口
│   ├── config/           # 配置
│   ├── model/            # 数据模型
│   ├── simulator/        # 模拟算法
│   ├── mqtt/             # MQTT 发布者
│   ├── influx/           # InfluxDB 存储
│   └── api/              # HTTP/WebSocket 服务
├── frontend/
│   ├── src/
│   │   ├── App.jsx       # 主组件（含去重逻辑）
│   │   └── components/
│   │       └── ZoneChart.jsx  # 区域图表
│   └── vite.config.js
├── docker-compose.yml    # 基础设施
└── mosquitto.conf        # MQTT 配置
```
