# 无人机编队控制Web系统

一个功能完整的无人机编队控制Web系统，支持最多5台无人机的编队控制、单机控制、飞行日志记录和任务回放。

## 功能特性

### 编队控制
- **队形配置**: 支持一字形、V字形、圆形、三角形四种队形
- **一键起飞/降落**: 所有无人机同时起飞/降落
- **编队移动**: 保持队形整体平移（前后左右上下）
- **队形保持**: 自动保持相对位置，误差<0.5米

### 单机控制
- **锁定/解锁**: 单独控制无人机是否参与编队
- **手动控制**: 前后左右上下六方向控制
- **速度调节**: 慢速/正常/快速三档速度

### 地图显示
- **实时位置**: 所有无人机位置实时更新
- **队形预览**: 显示编队队形轮廓线
- **状态显示**: 显示无人机连接、起飞、锁定状态

### 飞行记录
- **自动记录**: 起飞时自动开始记录飞行数据
- **数据记录**: 位置、速度、电池、GPS星数
- **任务回放**: 支持暂停、快进、跳转的飞行回放

## 技术栈

### 后端
- Node.js + Express
- MQTT (Mosquitto)
- Socket.IO
- 文件系统存储日志

### 前端
- React 18
- Mapbox GL JS
- Ant Design
- Socket.IO Client

### 基础设施
- Docker + Docker Compose
- Mosquitto MQTT Broker

## 快速开始

### 方式一: Docker Compose (推荐)

```bash
# 克隆项目
cd drone-swarm-control

# 启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps

# 访问前端: http://localhost:3000
```

### 方式二: 本地开发

#### 1. 启动 MQTT Broker
```bash
# 使用 Docker 启动 Mosquitto
docker run -d --name mosquitto -p 1883:1883 -p 9001:9001 eclipse-mosquitto:2.0
```

#### 2. 启动后端服务
```bash
cd backend
npm install
npm run dev
```

#### 3. 启动无人机模拟器
```bash
# 新开终端
cd backend
npm run simulator
```

#### 4. 启动前端
```bash
# 新开终端
cd frontend
npm install
npm start
```

## 项目结构

```
drone-swarm-control/
├── backend/
│   ├── src/
│   │   ├── controllers/
│   │   │   └── FormationController.js  # 编队控制算法
│   │   ├── managers/
│   │   │   ├── DroneManager.js         # 无人机管理器
│   │   │   └── ReplayManager.js        # 回放管理器
│   │   ├── simulator/
│   │   │   └── drone-simulator.js      # 无人机模拟器
│   │   ├── utils/
│   │   │   └── FlightLogger.js         # 飞行日志工具
│   │   └── server.js                   # 服务器入口
│   ├── logs/                           # 飞行日志存储
│   ├── package.json
│   ├── Dockerfile
│   └── .env
├── frontend/
│   ├── src/
│   │   ├── components/
│   │   │   ├── MapView.js              # 地图组件
│   │   │   ├── FormationControl.js     # 编队控制组件
│   │   │   ├── DroneList.js            # 无人机列表
│   │   │   ├── DroneControl.js         # 单机控制组件
│   │   │   └── ReplayPanel.js          # 任务回放面板
│   │   ├── App.js
│   │   ├── index.js
│   │   └── index.css
│   ├── package.json
│   ├── Dockerfile
│   └── .env
├── mosquitto/
│   └── config/
│       └── mosquitto.conf              # MQTT配置
├── docker-compose.yml
└── README.md
```

## API 接口

### HTTP API
- `GET /api/drones` - 获取所有无人机状态
- `GET /api/formation` - 获取当前编队信息
- `GET /api/logs` - 获取飞行日志列表
- `GET /api/logs/:id` - 获取指定日志详情

### Socket.IO 事件

#### 服务端发送
- `drones:list` - 无人机列表
- `drone:telemetry` - 无人机遥测数据
- `drone:status` - 无人机状态更新
- `drone:locked` - 无人机锁定状态
- `formation:current` - 当前编队信息
- `replay:started` - 回放开始
- `replay:progress` - 回放进度
- `replay:finished` - 回放结束

#### 客户端发送
- `formation:set` - 设置编队队形
- `formation:takeoff` - 编队起飞
- `formation:land` - 编队降落
- `formation:move` - 编队移动
- `drone:lock` - 锁定无人机
- `drone:unlock` - 解锁无人机
- `drone:manual` - 手动控制无人机
- `replay:start` - 开始回放
- `replay:pause` - 暂停回放
- `replay:stop` - 停止回放
- `replay:seek` - 跳转回放位置

## MQTT 主题

- `drones/{droneId}/telemetry` - 遥测数据
- `drones/{droneId}/status` - 状态数据
- `drones/{droneId}/command` - 控制命令

## 使用说明

### 编队控制
1. 打开首页，右侧选择"编队控制"标签
2. 选择队形类型（一字形/V字形/圆形/三角形）
3. 调整队形间距
4. 设置起飞高度，点击"一键起飞"
5. 使用方向键控制编队移动
6. 任务完成后点击"一键降落"

### 单机控制
1. 选择"单机控制"标签
2. 从下拉列表选择无人机
3. 查看无人机状态信息
4. 使用锁定开关控制是否参与编队
5. 使用方向键手动控制无人机

### 任务回放
1. 选择"任务回放"标签
2. 从列表选择飞行日志
3. 点击"开始回放"
4. 使用播放控制暂停/继续/停止
5. 拖动进度条跳转位置
6. 调整回放速度（0.5x/1x/2x/4x）

## 配置说明

### Mapbox Token
在 `frontend/.env` 中配置：
```
REACT_APP_MAPBOX_TOKEN=your_mapbox_token_here
```

### 队形保持精度
在 `backend/.env` 中配置：
```
FORMATION_TOLERANCE=0.5
```
单位：米，默认0.5米

### 模拟器更新频率
在 `backend/.env` 中配置：
```
SIMULATOR_UPDATE_INTERVAL=100
```
单位：毫秒，默认100ms

## 与真实无人机集成

当前系统使用软件模拟器。要连接真实无人机（DroneKit-SITL）：

1. 安装 DroneKit 和 SITL
2. 修改 `drone-simulator.js` 替换为真实 DroneKit 连接
3. 确保 MQTT 主题格式一致
4. 调整编队控制参数适配真实无人机性能

## 注意事项

1. **Mapbox Token**: 首次使用需要配置有效的 Mapbox Access Token
2. **MQTT Broker**: 确保 Mosquitto 服务正常运行
3. **浏览器兼容**: 推荐使用 Chrome/Edge 浏览器
4. **数据存储**: 飞行日志以 JSON 文件存储在 `backend/logs` 目录

## 许可证

MIT License
