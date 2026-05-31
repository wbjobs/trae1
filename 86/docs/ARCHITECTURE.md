# 无人机编队控制Web系统 - 架构文档

## 系统架构概述

本系统采用分布式架构，包含以下核心组件：

### 1. **前端展示层 (React + Mapbox GL)
2. **后端服务层 (Node.js + Express + Socket.IO)
3. **消息中间件 (MQTT Broker - Mosquitto)
4. **模拟层 (DroneSimulator / DroneKit-SITL)

### 数据流

```
┌─────────────────┐     WebSocket      ┌─────────────────┐
│   React前端     │◄──────────────────►│  Node.js后端   │
│  (Mapbox GL)   │                    │ (Express+Socket.IO) │
└────────┬────────┘                    └────────┬────────┘
         │                               │
         │                               │ MQTT Pub/Sub
         │                               ▼
         │                        ┌─────────────────┐
         │                        │  Mosquitto MQTT  │
         │                        │    Broker         │
         │                        └────────┬────────┘
         │                                 │
         │                                 │ MQTT Pub/Sub
         ▼                                 ▼
┌─────────────────┐                    ┌─────────────────┐
│   地图渲染      │                    │  无人机模拟器   │
│  编队显示      │◄──────────────────►│ (5台模拟无人机│
│  轨迹回放      │                    │ (DroneKit-SITL)│
└─────────────────┘                    └─────────────────┘
```

## 核心模块说明

### 1. 后端模块

#### DroneManager (`src/managers/DroneManager.js`)
- 管理所有无人机的状态
- 处理无人机命令分发
- 维护无人机实时数据存储
- 提供无人机锁定/解锁机制

#### FormationController (`src/controllers/FormationController.js`)
- 编队队形算法（一字形、V字形、圆形、三角形）
- 编队位置保持控制（误差<0.5米）
- 编队移动控制
- 编队起飞/降落控制

#### FlightLogger (`src/utils/FlightLogger.js`)
- 飞行日志记录（位置、速度、电池、GPS星数）
- 日志存储与管理
- 日志查询接口

#### ReplayManager (`src/managers/ReplayManager.js`)
- 任务回放控制
- 播放速度控制（0.5x, 1x, 2x, 4x）
- 时间轴定位

### 2. 前端模块

#### MapView (`src/components/MapView.js`)
- Mapbox GL地图集成
- 无人机Marker渲染
- 编队连接线渲染
- 轨迹绘制

#### FormationControl (`src/components/FormationControl.js`)
- 队形选择
- 间距调整
- 起飞/降落控制
- 编队移动控制

#### DroneControl (`src/components/DroneControl.js`)
- 单机选择
- 锁定/解锁
- 手动控制（前后左右上下）
- 状态显示

#### ReplayPanel (`src/components/ReplayPanel.js`)
- 日志列表
- 回放控制
- 时间轴控制
- 速度调节

## MQTT Topic规范

### 无人机遥测数据
- Topic: `drones/{droneId}/telemetry`
- 内容:
```json
{
  "position": { "lat": number, "lng": number, "alt": number },
  "velocity": { "x": number, "y": number, "z": number },
  "battery": 100,
  "gpsSatellites": 12,
  "heading": 90,
  "timestamp": 1234567890
}
```

### 无人机状态
- Topic: `drones/{droneId}/status`
- 内容:
```json
{
  "connected": true,
  "armed": true,
  "mode": "GUIDED",
  "timestamp": 1234567890
}
```

### 无人机命令
- Topic: `drones/{droneId}/command`
- 内容:
```json
{
  "command": "takeoff|land|goto|arm|disarm|lock|unlock",
  "payload": {},
  "timestamp": 1234567890
}
```

## Socket.IO事件规范

### 服务端 -> 客户端

- `drones:list` - 无人机列表
- `drone:telemetry` - 无人机遥测更新
- `drone:status` - 无人机状态更新
- `drone:locked` - 无人机锁定状态
- `formation:current` - 当前队形信息
- `replay:*` - 回放相关事件

### 客户端 -> 服务端

- `formation:set` - 设置队形
- `formation:takeoff` - 编队起飞
- `formation:land` - 编队降落
- `formation:move` - 编队移动
- `drone:lock/unlock` - 锁定/解锁无人机
- `drone:manual` - 手动控制
- `replay:*` - 回放控制

## 编队算法

### 位置保持控制

系统每500ms检查一次每架无人机与目标位置的距离，当距离超过0.5米时，重新发送目标位置命令。

### 队形计算

- **一字形**: 等间距水平排列
- **V字形**: 领头机在前，后续飞机分左右两侧排列
- **圆形**: 等角度环绕中心点
- **三角形**: 等边三角形网格排列

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| FORMATION_TOLERANCE | 0.5米 | 队形保持误差容限 |
| SIMULATOR_UPDATE_INTERVAL | 100ms | 模拟器更新频率 |
| 默认间距 | 10米 | 无人机间距 |
| 默认高度 | 10米 | 起飞高度 |