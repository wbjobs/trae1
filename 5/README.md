# 前端性能监控溯源系统

浏览器端前端性能监控溯源系统，支持性能数据采集、异常捕获、页面渲染监控。

## 功能特性

- **性能指标采集**: FP/FCP/LCP/TTFB/DOM Ready/Load Time
- **异常捕获**: JS运行时错误、Promise拒绝、资源加载错误、HTTP请求错误
- **渲染监控**: FPS帧率、内存使用、长任务、卡顿检测
- **数据上报**: 支持单次上报和批量上报
- **历史查询**: 性能趋势统计、错误溯源分析、渲染数据汇总

## 项目结构

```
.
├── backend/                  # 后端服务
│   ├── main.py              # FastAPI主入口
│   ├── config.py            # 配置文件
│   ├── database.py          # InfluxDB连接管理
│   ├── models/              # 数据模型
│   │   ├── __init__.py
│   │   └── schemas.py       # Pydantic模型定义
│   ├── routers/             # 路由
│   │   ├── __init__.py
│   │   ├── data.py          # 数据上报接口
│   │   └── query.py         # 数据查询接口
│   ├── services/            # 业务逻辑
│   │   ├── __init__.py
│   │   ├── data_service.py  # 数据存储服务
│   │   └── query_service.py # 数据查询服务
│   ├── requirements.txt     # 依赖列表
│   └── .env.example         # 环境变量示例
├── frontend/                # 前端
│   ├── index.html           # 监控仪表板
│   ├── test.html            # 测试页面
│   ├── css/
│   │   └── style.css        # 样式文件
│   └── js/
│       ├── performance-monitor.js  # 性能采集模块
│       ├── error-tracker.js        # 错误追踪模块
│       ├── renderer-monitor.js     # 渲染监控模块
│       ├── api.js                  # API客户端
│       ├── main.js                 # 主入口
│       └── dashboard.js            # 仪表板逻辑
└── start.bat                # 启动脚本
```

## 快速开始

### 1. 启动InfluxDB

确保已安装并启动InfluxDB 2.x:

```bash
# Docker方式
docker run -d --name influxdb -p 8086:8086 influxdb:2.7
```

### 2. 配置环境变量

复制 `.env.example` 为 `.env` 并修改配置:

```bash
cp backend/.env.example backend/.env
```

### 3. 启动后端服务

```bash
cd backend
pip install -r requirements.txt
python main.py
```

API文档: http://localhost:8000/docs

### 4. 打开前端页面

直接用浏览器打开 `frontend/index.html`

## API接口

### 数据上报

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/report/performance` | 上报性能数据 |
| POST | `/api/report/error` | 上报错误数据 |
| POST | `/api/report/renderer` | 上报渲染数据 |
| POST | `/api/report/batch` | 批量上报数据 |

### 数据查询

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/query/performance/trends` | 查询所有性能趋势 |
| POST | `/api/query/performance/trend/{metric}` | 查询单个指标趋势 |
| POST | `/api/query/errors/summary` | 错误汇总统计 |
| POST | `/api/query/errors/details` | 错误详情查询 |
| POST | `/api/query/renderer/summary` | 渲染数据汇总 |
| POST | `/api/query/stats` | 应用统计数据 |

## 前端使用

```javascript
// 引入监控脚本
<script src="js/performance-monitor.js"></script>
<script src="js/error-tracker.js"></script>
<script src="js/renderer-monitor.js"></script>
<script src="js/main.js"></script>

// 初始化监控
const monitor = new FrontendMonitor({
  appId: 'your-app-id',
  userId: 'user-123',
  reportUrl: 'http://localhost:8000/api/report',
  apiUrl: 'http://localhost:8000/api',
  sampleRate: 1  // 采样率 0-1
});

// 手动捕获错误
try {
  // ...
} catch (error) {
  monitor.captureError(error);
}

// 查询历史数据
const result = await monitor.queryPerformanceTrends(
  '2024-01-01T00:00:00Z',
  '2024-01-02T00:00:00Z'
);
```

## 技术栈

- **后端**: FastAPI + Pydantic + InfluxDB
- **前端**: 原生JavaScript + PerformanceObserver API
- **数据库**: InfluxDB 2.x 时序数据库

## 性能指标说明

| 指标 | 说明 | 良好 | 需改进 | 较差 |
|------|------|------|--------|------|
| FP | 首次绘制 | <1.8s | 1.8-3s | >3s |
| FCP | 首次内容绘制 | <1.8s | 1.8-3s | >3s |
| LCP | 最大内容绘制 | <2.5s | 2.5-4s | >4s |
| TTFB | 首字节时间 | <0.8s | 0.8-1.8s | >1.8s |
| FPS | 帧率 | >50 | 30-50 | <30 |

## License

MIT
