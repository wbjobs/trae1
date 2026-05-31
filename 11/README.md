# 企业内部接口文档自动化管理平台

一套覆盖前后端的完整解决方案，实现接口文档的自动生成、在线调试、版本回溯、成员权限管控与团队协作编辑。

## 功能特性

- 文档编辑：基于 Monaco + Markdown 的可视化富文本编辑体验
- Swagger/OpenAPI 自动解析：一键导入，自动生成接口文档
- 在线调试：内置 HTTP 请求沙箱，支持 Query/Header/Body/Cookie 调试
- 版本管理：历史版本时间线，任意版本对比 diff，一键回滚
- 权限管控：管理员/编辑者/只读三级角色，细粒度权限校验
- 团队协作：项目级成员管理，邀请/移除/角色变更
- 多项目多文档分类：树形分类管理，支持标签与搜索

## 目录结构

```
.
├── frontend/        # Vue3 + Vite + TypeScript 前端
└── backend/         # NestJS + TypeORM + MySQL 后端
```

## 快速开始

### 前置条件
- Node.js >= 18
- MySQL >= 5.7

### 后端

```bash
cd backend
npm install
# 修改 .env 中的数据库连接
npm run start:dev
```

后端默认端口 `3000`，Swagger UI 地址：`http://localhost:3000/api-docs`

### 前端

```bash
cd frontend
npm install
npm run dev
```

前端默认端口 `5173`，访问 `http://localhost:5173`

默认管理员账号：`admin` / `admin123`（首次启动自动创建）

## 架构说明

### 前端
- **框架**：Vue 3 + TypeScript + Vite 5
- **状态管理**：Pinia
- **UI 库**：Element Plus
- **编辑器**：Monaco Editor（Markdown/JSON）
- **HTTP 客户端**：Axios（已封装统一拦截器）
- **Markdown 渲染**：marked
- **Diff 对比**：diff

模块：
- `views/auth` 登录/注册
- `views/dashboard` 控制台统计
- `views/projects` 项目管理
- `views/docs` 文档列表/详情/编辑
- `views/docs/version` 版本对比
- `views/docs/debug` 在线调试
- `views/members` 成员管理
- `components` 通用组件（ParamsEditor/JsonEditor/KeyValueEditor 等）

### 后端
- **框架**：NestJS 10 + TypeORM
- **数据库**：MySQL
- **鉴权**：JWT + Passport
- **接口文档**：@nestjs/swagger
- **HTTP**：Axios（在线调试代理）

模块：
- `auth` 登录/注册/JWT 鉴权
- `users` 用户管理
- `projects` 项目 CRUD + Swagger 导入
- `docs` 文档 CRUD + 在线调试代理
- `versions` 版本历史/对比/回滚
- `members` 成员与角色
- `permissions` 权限校验
- `swagger_parser` Swagger/OpenAPI 解析与同步

## 数据库模型

- `users`：用户与角色
- `projects`：项目及 Swagger 源地址
- `docs`：接口文档（含请求/响应 schema）
- `doc_versions`：文档版本快照
- `members`：项目-成员-角色 关联

## 接口总览

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | /api/auth/login | 登录 |
| POST | /api/auth/register | 注册 |
| GET | /api/projects | 项目列表 |
| POST | /api/projects | 新建项目 |
| POST | /api/projects/:id/import-swagger | 导入 Swagger |
| POST | /api/projects/:id/sync-swagger | 同步 Swagger |
| GET | /api/projects/:pid/docs | 文档列表 |
| POST | /api/docs | 新建文档 |
| PUT | /api/docs/:id | 更新文档 |
| POST | /api/docs/debug | 在线调试 |
| GET | /api/docs/:id/versions | 版本列表 |
| POST | /api/docs/:id/versions/compare | 版本对比 |
| POST | /api/docs/:id/versions/:vid/rollback | 版本回滚 |
| GET | /api/members | 成员列表 |
| POST | /api/members/invite | 邀请成员 |

完整接口文档可在后端启动后访问 `/api-docs`。

## 许可证

MIT
