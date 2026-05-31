# 组件库按需加载管理平台

> 前端 React + TypeScript | 后端 NestJS + PostgreSQL + TypeORM

## 项目架构

```
.
├── backend/                # NestJS 后端服务
│   └── src/
│       ├── main.ts         # 入口
│       ├── app.module.ts   # 根模块
│       ├── components/     # 组件模块 (实体/服务/控制器) — 含热度统计
│       ├── versions/       # 版本模块 (实体/服务/控制器) — 含版本回滚
│       ├── dependencies/   # 依赖检测模块 (实体/服务/控制器) — 含缓存优化
│       ├── preview/        # 预览与按需引入模块
│       ├── docs/           # 文档生成模块
│       └── bundle/         # 打包压缩分析模块
└── frontend/               # React + TS 前端
    └── src/
        ├── main.tsx        # 入口
        ├── App.tsx         # 路由与布局
        ├── pages/          # 页面组件
        ├── services/api.ts # HTTP 客户端
        ├── stores/         # Zustand 状态管理
        ├── styles/         # 全局样式
        └── types/          # TypeScript 类型定义
```

## 快速开始

### 后端

```bash
cd backend
npm install
# 配置 .env 中的 PostgreSQL 连接
npm run start:dev
# Swagger: http://localhost:4000/api/docs
```

### 前端

```bash
cd frontend
npm install
npm run dev
# 访问 http://localhost:5173
```

## 核心功能

### 1. 组件热度统计
- 每个组件记录 `downloadCount`（下载）、`previewCount`（预览）、`referenceCount`（引用）
- 自动计算 `popularityScore = 下载×3 + 预览×1 + 引用×2`
- 支持按热度排序、Top N 热度榜单
- 前端组件管理页展示热度 Top 5 榜单

### 2. 版本回滚
- **软回滚** (`POST /versions/:id/rollback`)：将历史版本标记为 latest，保留原版本号
- **硬回滚** (`POST /versions/:id/rollback-clone`)：克隆历史版本内容，递增版本号并设为 latest
- 前端版本列表提供两种回滚操作按钮

### 3. 依赖解析优化
- **内存缓存**：Range 对象和 valid 结果缓存，TTL 5 分钟
- **两两求交**：使用 `intersects` 进行精确范围求交
- **智能建议**：使用 `maxSatisfying` 验证建议版本能否满足所有范围
- **批量分析**：`batchAnalyze` 支持并行处理多组版本 ID

### 4. 打包压缩分析
- **体积估算**：基于导出成员数、依赖数、源码长度估算原始体积
- **压缩模拟**：按体积阶梯估算 Gzip/Brotli 压缩后体积
- **Tree Shaking 潜力**：根据导出成员数评估优化空间
- **未使用检测**：识别 README/预览源码中未引用的导出
- **优化建议**：根据包体积、压缩率、依赖数智能生成建议
- **版本对比**：两个版本间体积变化百分比对比

### 5. 在线预览 & 按需引入
- iframe 沙箱渲染组件预览源码
- 生成 ESM/CJS/UNPKG 引入代码
- Tree Shaking 路径生成
- 空预览源异常处理

### 6. 文档生成
- 结构化文档数据
- Markdown 文档下载

## API 概览

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/components` | 创建组件 |
| GET | `/api/components` | 组件列表（支持 `sortBy=popularity`） |
| GET | `/api/components/stats/top` | 热度 Top N |
| GET | `/api/components/:id` | 组件详情 |
| PUT | `/api/components/:id` | 更新组件 |
| DELETE | `/api/components/:id` | 删除组件 |
| POST | `/api/components/:id/stats/download` | 记录下载 |
| POST | `/api/components/:id/stats/preview` | 记录预览 |
| POST | `/api/components/:id/stats/reference` | 记录引用 |
| POST | `/api/versions` | 发布新版本 |
| GET | `/api/versions/component/:id` | 组件所有版本 |
| GET | `/api/versions/component/:id/latest` | 最新版本 |
| GET | `/api/versions/component/:id/suggest` | 建议下一个版本号 |
| PUT | `/api/versions/:id/latest` | 标记为最新 |
| POST | `/api/versions/:id/rollback` | 软回滚 |
| POST | `/api/versions/:id/rollback-clone` | 硬回滚（克隆） |
| DELETE | `/api/versions/:id` | 删除版本 |
| POST | `/api/dependencies/analyze` | 依赖冲突检测 |
| GET | `/api/preview/:versionId` | 获取预览与按需引入配置 |
| GET | `/api/bundle/:versionId` | 打包体积分析 |
| GET | `/api/bundle/compare/:a/:b` | 版本体积对比 |
| GET | `/api/docs/:name` | 获取结构化文档 |
| GET | `/api/docs/:name/markdown` | 下载 Markdown 文档 |

## 性能优化要点

- **依赖范围缓存**：避免重复解析 semver Range 对象
- **批量并行查询**：`Promise.all` 并行获取组件列表和热度榜单
- **智能体积估算**：基于特征的体积预测，无需实际打包
- **Tree Shaking 评估**：量化优化空间，辅助决策
