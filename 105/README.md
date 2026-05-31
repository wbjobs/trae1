# OAuth2 权限审计系统

一个完整的 OAuth2 授权服务器，具备权限审计、日志记录和可视化功能。

## 功能特性

- **OAuth2 授权服务器** - 支持授权码、客户端凭证等授权流程
- **灵活的授权配置** - 用户可选择授权范围（scope）和有效期（1小时/7天/永久）
- **Scope 分组展示** - 将权限分为基本信息、读写权限、敏感信息、高危权限四组
  - 每项权限附带示例说明（如"读取你的昵称和头像"）
  - 高危权限（admin:all等）标红并强制二次确认
- **权限回收** - 用户可随时撤销已授权的应用
- **设备追踪** - 记录授权时的 IP 地址和设备指纹
- **一键登出** - 支持查看设备信息并一键登出所有设备
- **API 调用日志** - 记录所有通过 OAuth2 授权的 API 调用
- **Elasticsearch 存储** - 审计日志存储到 Elasticsearch，保留 180 天
- **异常检测** - 自动检测异常调用模式（如深夜调用）
- **ML 异常检测** - 基于 Isolation Forest 机器学习算法的行为分析
  - 分析用户授权行为模式（常用授权时间、常用应用、授权时长偏好）
  - 检测异常授权（如凌晨3点授权某个新应用、用户从未授权过的应用类型）
  - 发送短信/邮件通知用户确认授权行为
  - 用户可标记"是我本人操作"或"不是，立即撤销"
  - 模型使用历史90天授权数据训练，每周自动更新
  - 可配置异常评分阈值（默认0.7）
- **审计 Dashboard** - Vue3 + ECharts 可视化展示
  - Top 应用调用统计
  - Top 用户调用统计
  - 异常请求列表
  - 风险事件统计
- **历史记录查询** - 支持按用户、应用、时间范围检索

## 技术栈

### 后端
- Spring Boot 3.2
- Spring Security
- Spring Authorization Server
- Spring Data JPA
- Spring Data Elasticsearch
- H2 数据库（内存）
- Lombok

### 前端
- Vue 3
- Vite
- Ant Design Vue
- ECharts
- Axios
- Vue Router

## 快速开始

### 前置要求
- JDK 17+
- Node.js 18+
- Elasticsearch 8.x（运行在 localhost:9200）

### 1. 启动 Elasticsearch

```bash
# 使用 Docker 快速启动
docker run -d --name elasticsearch \
  -p 9200:9200 \
  -e "discovery.type=single-node" \
  -e "xpack.security.enabled=false" \
  docker.elastic.co/elasticsearch/elasticsearch:8.11.0
```

### 2. 启动后端

```bash
cd backend
./mvnw spring-boot:run
```

后端服务运行在 http://localhost:8080

### 3. 启动前端

```bash
cd frontend
npm install
npm run dev
```

前端服务运行在 http://localhost:3000

## 项目结构

```
oauth2-audit-system/
├── backend/
│   ├── src/
│   │   └── main/
│   │       ├── java/com/oauth2/audit/
│   │       │   ├── config/           # 安全和初始化配置
│   │       │   ├── controller/       # REST API 控制器
│   │       │   ├── entity/           # 数据实体
│   │       │   ├── repository/       # 数据访问层
│   │       │   └── service/          # 业务逻辑层
│   │       └── resources/
│   │           └── application.yml
│   └── pom.xml
├── frontend/
│   ├── src/
│   │   ├── views/                    # 页面组件
│   │   ├── router/                   # 路由配置
│   │   ├── App.vue
│   │   └── main.js
│   ├── index.html
│   ├── vite.config.js
│   └── package.json
└── README.md
```

## 预置数据

系统启动时会自动创建以下测试数据：

**用户：**
- admin / admin123
- user1 / user123
- user2 / user123

**客户端应用：**
- demo-client (演示应用)
- analytics-app (分析应用)

## API 端点

### 授权管理
- `POST /api/authorizations` - 创建授权
- `DELETE /api/authorizations/{id}` - 撤销授权
- `DELETE /api/authorizations/user/{userId}/all` - 撤销用户所有授权
- `GET /api/authorizations/user/{userId}` - 获取用户有效授权
- `GET /api/authorizations/user/{userId}/history` - 获取用户授权历史

### Scope 信息
- `GET /api/scopes` - 获取所有 scope 配置
- `GET /api/scopes/grouped` - 按分组获取 scope 信息
- `GET /api/scopes/high-risk` - 检查是否包含高危权限

### 审计查询
- `GET /api/audit/dashboard/stats` - 获取 Dashboard 统计数据
- `GET /api/audit/logs/user/{userId}` - 按用户查询日志
- `GET /api/audit/logs/client/{clientId}` - 按应用查询日志
- `GET /api/audit/logs/range?start=&end=` - 按时间范围查询
- `GET /api/audit/logs/anomalies` - 查询异常请求

### 示例资源 API
- `GET /api/resource/profile` - 访问 profile 资源
- `GET /api/resource/data` - 读取数据
- `POST /api/resource/data` - 创建数据

### 测试接口
- `POST /api/test/generate-logs` - 生成测试日志

### 风险事件（ML异常检测）
- `GET /api/risk-events` - 获取所有风险事件
- `GET /api/risk-events/pending` - 获取待处理风险事件
- `GET /api/risk-events/user/{userId}` - 获取用户风险事件
- `GET /api/risk-events/level/{level}` - 按等级获取风险事件
- `GET /api/risk-events/statistics` - 获取风险统计
- `POST /api/risk-events/{eventId}/confirm-self` - 确认本人操作
- `POST /api/risk-events/{eventId}/revoke` - 撤销授权
- `POST /api/risk-events/{eventId}/dismiss` - 忽略事件
- `GET /api/risk-events/model/status` - 获取模型状态
- `POST /api/risk-events/model/train` - 触发模型训练

## 使用说明

1. 访问 http://localhost:3000 打开审计 Dashboard
2. 在"授权管理"页面可以创建和撤销用户授权
   - 点击"新建授权"可以创建新的授权
   - 授权时会显示 scope 的分组信息和高危权限警告
   - 支持查看每个授权的设备信息（IP、设备指纹等）
   - 可以一键登出用户的所有设备
3. 在"审计日志"页面可以查询历史 API 调用记录
4. Dashboard 会展示实时的调用统计和异常请求
5. 在"风险事件"页面可以查看 ML 检测到的异常授权
   - 查看待处理的风险事件
   - 标记"本人操作"或"立即撤销"
   - 查看风险事件统计和模型状态
   - 手动触发模型训练
6. 访问 `/authorize/{clientId}?scopes=scope1,scope2` 可以打开授权确认页面

## 注意事项

- 审计日志需要显式调用 AuditLogService.logRequest() 来记录
- 当前使用 H2 内存数据库，重启后数据会丢失
- Elasticsearch 必须在应用启动前运行
- 生产环境建议使用持久化数据库（如 MySQL/PostgreSQL）

## ML 异常检测配置

在 `application.yml` 中配置 ML 相关参数：

```yaml
ml:
  anomaly-detection:
    enabled: true          # 启用异常检测
    threshold: 0.7         # 异常评分阈值 (0.0-1.0)
    training-days: 90      # 训练数据天数
    update-cron: "0 0 2 ? * SUN"  # 每周日凌晨2点更新模型
  notification:
    sms:
      enabled: false       # 启用短信通知
      provider: twilio
      from-number: "+1234567890"
    email:
      enabled: true        # 启用邮件通知
      smtp-host: smtp.example.com
      smtp-port: 587
      from-address: noreply@example.com
```

## 许可证

MIT License
