# 日志脱敏审核系统

日志脱敏审核Web应用，提供日志上传、脱敏规则配置、日志预览、批量脱敏、审核记录溯源等功能。

## 功能特性

- **日志上传**：支持 .log/.txt/.json/.csv 等格式，最大 20MB，自动识别日志格式
- **脱敏规则配置**：自定义正则/关键字/固定值规则，支持优先级、区分大小写、启用/禁用
- **日志预览**：分页查看原始日志内容
- **批量脱敏**：基于规则引擎对日志进行批量脱敏处理
- **违规拦截**：命中高危规则的日志可自动拦截，禁止下载
- **审核溯源**：完整的审核记录和违规明细查询
- **多格式支持**：plain、json、csv、nginx、syslog、apache
- **实时脱敏测试**：在规则配置页实时测试脱敏效果

## 技术栈

- **前端**：Vue 3 + Element Plus（CDN引入，无需构建）
- **后端**：Node.js + Express
- **数据库**：MySQL
- **核心依赖**：multer、mysql2、dayjs、uuid

## 项目结构

```
.
├── client/
│   └── index.html        # 前端单页应用
├── server/
│   ├── config/
│   │   └── index.js      # 服务配置
│   ├── src/
│   │   ├── index.js      # Express入口
│   │   ├── db/
│   │   │   ├── pool.js   # MySQL连接池
│   │   │   └── init.js   # 数据库初始化脚本
│   │   ├── controllers/
│   │   │   ├── ruleController.js   # 规则CRUD
│   │   │   └── logController.js    # 日志上传/脱敏/审核
│   │   ├── routes/
│   │   │   ├── ruleRoutes.js
│   │   │   └── logRoutes.js
│   │   └── utils/
│   │       ├── logParser.js        # 多格式日志解析
│   │       └── maskEngine.js       # 脱敏引擎
│   ├── uploads/          # 上传文件存储
│   ├── masked/           # 脱敏后文件存储
│   └── package.json
└── README.md
```

## 快速开始

### 1. 安装依赖

```bash
cd server
npm install
```

### 2. 配置数据库

编辑 `server/config/index.js`，修改数据库连接信息：

```js
db: {
  host: '127.0.0.1',
  port: 3306,
  user: 'root',
  password: '你的密码',
  database: 'log_mask'
}
```

### 3. 初始化数据库

```bash
npm run init-db
```

该命令会自动创建数据库、数据表，并插入6条默认脱敏规则（手机号、身份证、邮箱、银行卡、IP、密码关键字）。

### 4. 启动后端服务

```bash
npm start
```

服务启动于 `http://localhost:3000`

### 5. 打开前端

直接用浏览器打开 `client/index.html` 即可。

## API接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/rules | 查询规则列表 |
| GET | /api/rules/:id | 查询单个规则 |
| POST | /api/rules | 创建规则 |
| PUT | /api/rules/:id | 更新规则 |
| DELETE | /api/rules/:id | 删除规则 |
| PUT | /api/rules/:id/toggle | 启用/禁用规则 |
| POST | /api/logs/upload | 上传日志文件 |
| GET | /api/logs/preview | 预览日志 |
| POST | /api/logs/mask | 执行脱敏 |
| GET | /api/logs/audit | 查询审核记录 |
| GET | /api/logs/audit/:id | 审核详情 |
| GET | /api/logs/audit/:id/download | 下载脱敏文件 |
| POST | /api/logs/test | 实时脱敏测试 |

## 核心设计说明

### 脱敏引擎

- 规则按优先级排序，高优先级规则先匹配
- 对结构化日志（json/csv/nginx等）逐字段脱敏后重建原始格式，避免内容错乱
- 支持正则捕获组替换（如 `$1=****`）
- 违规严重程度根据规则优先级自动分级：high(≥90)、medium(≥60)、low

### 大文件处理

- 使用 `readline` 流式读取，分块写入脱敏文件
- 设置 `highWaterMark: 256KB` 优化内存占用
- 违规明细最多记录5000条，防止内存溢出

### 违规拦截

- 脱敏时若存在 `high` 级别的违规，且 `blockOnViolation=true`，则标记为 blocked
- blocked 状态的日志不提供脱敏文件下载
