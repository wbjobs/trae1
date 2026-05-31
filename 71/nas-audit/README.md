# NAS Audit Service

企业内网SMB共享目录文件操作审计系统。监控文件创建、删除、重命名、修改操作，存储到Elasticsearch，提供REST API和Web仪表盘。

## 架构概览

```
┌─────────────┐     ┌─────────────────┐     ┌──────────────┐
│  SMB Server  │────▶│  Python Monitor  │────▶│ Elasticsearch │
│ (文件共享)   │     │  (pysmb轮询)     │     │  (审计存储)    │
└─────────────┘     └─────────────────┘     └──────┬───────┘
                                                   │
┌─────────────┐     ┌─────────────────┐             │
│  React 前端  │◀────│  FastAPI REST    │◀────────────┘
│  (仪表盘)    │     │  (查询API)       │
└─────────────┘     └─────────────────┘
```

## 功能特性

- **文件操作监控**: 通过pysmb轮询SMB共享目录，检测文件创建、删除、重命名、修改
- **多维度过滤**: 按文件扩展名(.doc/.xlsx/.pdf等)过滤监控
- **Elasticsearch存储**: 审计日志自动索引，支持90天自动清理
- **REST API**: FastAPI提供完整的查询接口
- **仪表盘**: Kibana风格React前端，展示TOP活跃用户和操作趋势图
- **ILM策略**: Elasticsearch索引生命周期管理，自动滚动和过期删除

## 目录结构

```
nas-audit/
├── backend/
│   ├── src/
│   │   ├── core/
│   │   │   ├── config.py          # 配置加载
│   │   │   ├── event_models.py    # 事件数据模型
│   │   │   ├── smb_monitor.py     # SMB监控核心
│   │   │   ├── es_store.py        # Elasticsearch存储
│   │   │   └── logger.py          # 日志配置
│   │   ├── api/
│   │   │   ├── schemas.py         # API数据模型
│   │   │   └── service.py         # FastAPI服务
│   │   └── main.py                # 入口
│   ├── config.yaml                # 主配置文件
│   ├── requirements.txt
│   ├── Dockerfile
│   └── start.bat / start.sh
├── frontend/
│   ├── src/
│   │   ├── api/client.js          # API客户端
│   │   ├── components/
│   │   │   ├── Dashboard.js       # 仪表盘组件
│   │   │   └── EventLog.js        # 操作日志组件
│   │   ├── App.js
│   │   ├── index.js
│   │   └── index.css
│   ├── package.json
│   ├── Dockerfile
│   ├── nginx.conf
│   └── start.bat
├── docker-compose.yml
└── .gitignore
```

## 快速开始

### 方式一: Docker Compose (推荐)

```bash
# 克隆项目后
cd nas-audit

# 配置环境变量
cp backend/.env.example backend/.env
# 编辑 .env 填入SMB和Elasticsearch连接信息

# 启动所有服务
docker-compose up -d

# 查看日志
docker-compose logs -f backend
```

### 方式二: 本地开发

#### 前置条件
- Python 3.10+
- Node.js 18+
- Elasticsearch 8.x (运行在 localhost:9200)

#### 后端启动

```bash
cd backend

# 创建虚拟环境并安装依赖
python -m venv venv
source venv/bin/activate  # Windows: venv\Scripts\activate
pip install -r requirements.txt

# 配置
cp .env.example .env
# 编辑 .env 和 config.yaml

# 启动
python main.py
```

后端API文档: http://localhost:8000/docs

#### 前端启动

```bash
cd frontend

# 安装依赖
npm install

# 启动开发服务器
npm start
```

前端访问: http://localhost:3000

## 配置说明

### config.yaml

```yaml
smb:
  server: "192.168.1.100"       # SMB服务器地址
  port: 445                      # SMB端口
  username: "admin"              # SMB用户名
  password: ""                   # SMB密码
  domain: "CORP"                 # Windows域
  share_name: "SharedFiles"      # 共享名称
  watch_paths:                   # 监控路径列表
    - "/"
    - "Documents"
  poll_interval: 5               # 轮询间隔(秒)
  monitored_extensions:          # 监控的文件扩展名
    - ".doc"
    - ".docx"
    - ".xls"
    - ".xlsx"
    - ".pdf"

elasticsearch:
  hosts:
    - "http://localhost:9200"
  username: "elastic"
  password: ""
  index_prefix: "nas_audit"
  retention_days: 90             # 日志保留天数

api:
  host: "0.0.0.0"
  port: 8000
  cors_origins:
    - "http://localhost:3000"
```

## REST API

### 健康检查
```
GET /api/health
```

### 查询操作记录
```
GET /api/events
```

参数:
- `username` - 用户名过滤
- `start_time` - 开始时间 (Unix时间戳)
- `end_time` - 结束时间 (Unix时间戳)
- `extensions` - 文件扩展名，逗号分隔 (如: `.docx,.xlsx,.pdf`)
- `operation_type` - 操作类型: create/delete/rename/modify
- `file_path` - 文件路径关键字
- `source_ip` - 源IP地址
- `size` - 每页数量 (默认100，最大10000)
- `scroll_id` - 滚动查询ID

### 仪表盘数据
```
GET /api/dashboard?days=7&top_n=10
```

返回:
- `top_users` - TOP活跃用户
- `operation_trend` - 操作趋势(按天)
- `extension_stats` - 文件类型分布

### 手动录入事件
```
POST /api/events/manual
```

请求体:
```json
{
    "operation_type": "create",
    "file_path": "/Documents/report.docx",
    "username": "zhangsan",
    "source_ip": "192.168.1.50"
}
```

### 触发清理
```
POST /api/system/cleanup
```

## 数据模型

### FileOperationEvent

| 字段 | 类型 | 说明 |
|------|------|------|
| operation_type | enum | create/delete/rename/modify |
| file_path | string | 文件路径 |
| old_file_path | string | 重命名前的路径 |
| timestamp | float | Unix时间戳 |
| username | string | 操作用户 |
| source_ip | string | 源IP地址 |
| file_size | int | 文件大小(字节) |
| file_extension | string | 文件扩展名 |

## Elasticsearch索引

索引按天滚动: `nas_audit-YYYY.MM.DD`

ILM策略:
- Hot阶段: 每日滚动或达到50GB滚动
- Delete阶段: 90天后自动删除

## 开发

### 添加新的监控扩展名
编辑 `config.yaml` 中的 `monitored_extensions` 列表。

### 调整日志保留期
修改 `config.yaml` 中的 `retention_days`。

## 许可证

MIT License
