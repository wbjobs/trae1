# ConfigVCS - 配置文件版本管理系统

一个基于 IPFS 和 BadgerDB 的配置文件版本管理系统，提供 Git-like 的版本控制功能。

## 功能特性

- 📁 **文件存储**: 配置文件存储到 IPFS 网络，获得唯一的 CID（内容哈希）
- 🌳 **版本控制**: Git-like 的提交树结构，支持 commit -> parent -> root
- 🌿 **分支管理**: 创建、切换、删除分支
- 🔀 **AST 语义合并**: 基于抽象语法树的结构化三路合并，专门针对 YAML/JSON 优化
  - 解析为 AST 后按节点合并（识别添加、删除、修改）
  - 支持大文件（如 10MB+ Kubernetes manifest）正确处理
  - 冲突时提供可视化三路合并界面（3-way merge view）
  - 语法高亮，支持手动选择保留哪个父版本的修改
  - 自定义合并策略（如"数组元素按 name 字段去重合并"）
- 🏷 **版本标签**: 支持 Tag 标记重要版本
- 📝 **差异对比**: 高亮显示版本间的差异
- 📦 **配置拉取**: 支持通过 CID 或标签名拉取配置
- 💾 **本地缓存**: 元数据存储在 BadgerDB，提供快速访问
- 🔄 **多节点同步**: 通过 ipfs-cluster 实现多节点数据同步

## 技术栈

### 后端
- **Go**: 主要编程语言
- **IPFS (go-ipfs)**: 去中心化文件存储
- **BadgerDB**: 本地元数据存储
- **Gin**: HTTP 框架
- **go-diff**: 差异计算

### 前端
- **React**: 用户界面框架
- **Axios**: HTTP 客户端
- **React Router**: 路由管理

## 快速开始

### 前置条件

- Go 1.21+
- Node.js 18+
- IPFS 节点（本地或远程）
- Docker & Docker Compose（可选）

### 使用 Docker Compose 启动

```bash
docker-compose up -d
```

访问前端界面: http://localhost:3000

### 手动启动

#### 1. 启动 IPFS 节点

```bash
# 安装 IPFS
wget https://dist.ipfs.io/go-ipfs/v0.28.0/go-ipfs_v0.28.0_linux-amd64.tar.gz
tar -xvzf go-ipfs_v0.28.0_linux-amd64.tar.gz
cd go-ipfs
sudo bash install.sh

# 初始化并启动
ipfs init
ipfs daemon
```

#### 2. 启动后端

```bash
cd backend
go mod tidy
go run ./cmd/server
```

后端服务运行在 http://localhost:8080

#### 3. 启动前端

```bash
cd frontend
npm install
npm start
```

前端界面运行在 http://localhost:3000

## API 接口

### 配置文件

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /api/configs | 上传配置文件 |
| GET | /api/configs/:cid | 通过 CID 获取配置 |
| GET | /api/configs/tag/:tag | 通过标签获取配置 |
| GET | /api/configs/commit/:id/content | 获取提交的配置内容 |

### 提交

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/commits/:id | 获取提交详情 |
| GET | /api/commits/branch/:branch | 列出分支的所有提交 |
| POST | /api/commits | 创建新提交 |

### 分支

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/branches | 列出所有分支 |
| GET | /api/branches/:name | 获取分支详情 |
| POST | /api/branches | 创建分支 |
| DELETE | /api/branches/:name | 删除分支 |

### 标签

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/tags | 列出所有标签 |
| GET | /api/tags/:name | 获取标签详情 |
| POST | /api/tags | 创建标签 |
| DELETE | /api/tags/:name | 删除标签 |

### 差异对比

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/diff/commits/:a/:b | 对比两个提交 |
| GET | /api/diff/cid/:cid | 通过 CID 查看差异 |

### 合并

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /api/merge | 合并分支（支持 AST 语义合并和自定义策略） |
| POST | /api/merge/preview | 预览合并结果（不提交） |
| POST | /api/merge/resolve | 解决冲突并提交 |
| GET | /api/merge/strategy/default | 获取默认合并策略 |

### AST 差异对比

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/diff/ast/commits/:a/:b | AST 结构化差异对比 |

### 版本树

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/tree/:branch | 获取分支的版本树 |

## 配置环境变量

### 后端

| 变量 | 默认值 | 说明 |
|------|--------|------|
| IPFS_ADDR | localhost:5001 | IPFS 节点地址 |
| DB_PATH | ./data/badger | BadgerDB 数据目录 |
| SERVER_PORT | 8080 | 服务器端口 |

### 前端

| 变量 | 默认值 | 说明 |
|------|--------|------|
| REACT_APP_API_URL | http://localhost:8080/api | 后端 API 地址 |

## 项目结构

```
.
├── backend/
│   ├── cmd/
│   │   └── server/
│   │       └── main.go          # 入口文件
│   ├── pkg/
│   │   ├── api/
│   │   │   └── handler.go       # HTTP 处理器
│   │   ├── astmerge/            # AST 语义合并模块
│   │   │   ├── ast.go           # AST 节点定义和解析
│   │   │   └── merge.go         # 三路合并算法
│   │   ├── ipfs/
│   │   │   └── ipfs.go          # IPFS 客户端
│   │   ├── store/
│   │   │   └── store.go         # BadgerDB 存储
│   │   └── version/
│   │       ├── types.go         # 数据模型
│   │       └── service.go       # 版本控制服务
│   ├── go.mod
│   ├── go.sum
│   └── Dockerfile
├── frontend/
│   ├── public/
│   ├── src/
│   │   ├── components/          # 可复用组件
│   │   │   ├── ThreeWayMergeView.js   # 三路合并可视化界面
│   │   │   └── MergeStrategyConfig.js # 合并策略配置
│   │   ├── pages/               # 页面组件
│   │   │   ├── MergeResolvePage.js    # 冲突解决页面
│   │   │   └── ...
│   │   ├── services/            # API 服务
│   │   ├── App.js
│   │   ├── App.css
│   │   ├── index.js
│   │   └── index.css
│   ├── package.json
│   ├── nginx.conf
│   └── Dockerfile
└── docker-compose.yml
```

## 核心设计

### 数据模型

**Commit (提交)**:
```json
{
  "id": "commit-hash",
  "parent_id": "parent-commit-hash",
  "root_id": "root-commit-hash",
  "cid": "ipfs-content-hash",
  "message": "commit message",
  "author": "author name",
  "timestamp": "2024-01-01T00:00:00Z",
  "branch": "main"
}
```

**Branch (分支)**:
```json
{
  "name": "feature-branch",
  "commit": "latest-commit-hash"
}
```

**Tag (标签)**:
```json
{
  "name": "v1.0.0",
  "commit_id": "target-commit-hash",
  "message": "release version 1.0.0",
  "timestamp": "2024-01-01T00:00:00Z"
}
```

### 版本控制流程

1. 用户上传配置文件
2. 文件存储到 IPFS，获得 CID
3. 创建 Commit 对象，记录 CID 和父提交
4. 更新分支指向最新提交
5. 所有元数据存储到 BadgerDB

### 合并算法

#### AST 语义合并 (推荐)

系统采用基于抽象语法树 (AST) 的结构化三路合并算法，专门针对 YAML/JSON 配置文件优化：

1. **解析阶段**: 将三个版本（基准、源、目标）的配置文件解析为 AST 节点树
2. **对比阶段**: 递归遍历 AST，识别每个节点的变更类型（添加、删除、修改）
3. **合并阶段**:
   - 节点无变更：直接保留
   - 仅一个分支变更：应用该变更
   - 两个分支相同变更：应用变更
   - 两个分支不同变更：标记为冲突
4. **数组合并策略**:
   - 按键去重：按指定字段（如 `name`）匹配数组元素进行合并
   - 索引合并：按下标位置合并（适用于有序数组）
   - 组合合并：将两个数组合并（不去重）

**合并策略配置示例**:
```json
{
  "array_merge_by_keys": {
    ".spec.template.spec.containers": ["name"],
    ".spec.ports": ["name"],
    ".items": ["metadata.name"]
  },
  "prefer_source_paths": [".spec.replicas"],
  "combine_arrays_paths": [".spec.volumeMounts"]
}
```

#### 可视化冲突解决

当检测到冲突时，系统提供三路合并视图：
- 左侧：冲突列表，显示所有冲突节点路径
- 右侧：三个版本对比（源分支、目标分支、基准版本）
- 支持：
  - 点击选择保留哪个版本
  - 批量选择（全部源/全部目标/全部基准）
  - 语法高亮显示配置内容
  - 实时显示解决进度

## 支持的文件格式

- YAML (.yaml, .yml)
- JSON (.json)
- TOML (.toml)

## License

MIT
