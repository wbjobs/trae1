-- 安全文件分享系统

一个基于Vue3 + Koa2 + MySQL的内网文件加密共享平台

## 功能特性

- 🔐 AES-256-CBC 加密存储
- ⏰ 限时分享
- 🔑 权限分级（仅查看/可下载/完全控制
- 📋 访问密码保护
- 🔢 访问次数限制
- 🕐 过期自动销毁
- 📁 文件管理
- 🔗 分享链接管理

## 技术栈

### 后端
- Koa2
- Sequelize ORM
- MySQL
- AES-256-CBC 加密

### 前端
- Vue3
- Element Plus
- Pinia
- Vue Router
- Axios

## 快速开始

### 1. 数据库配置

创建MySQL数据库：

```sql
CREATE DATABASE secure_share CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

### 2. 环境变量

设置以下环境变量或修改 `server/src/config/database.js`：

```
DB_HOST=localhost
DB_PORT=3306
DB_NAME=secure_share
DB_USER=root
DB_PASS=your_password
PORT=3000
```

### 3. 安装依赖

```bash
# 安装后端依赖
cd server
npm install

# 安装前端依赖
cd client
npm install
```

### 4. 启动服务

```bash
# 启动后端服务 (端口: 3000)
cd server
npm run dev

# 启动前端开发服务 (端口: 5173)
cd client
npm run dev
```

### 5. 访问应用

打开浏览器访问: http://localhost:5173

## API 接口

### 文件接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /api/files/upload | 上传文件 |
| GET | /api/files | 获取文件列表 |
| GET | /api/files/:id | 获取文件详情 |
| GET | /api/files/:id/download | 下载文件 |
| GET | /api/files/:id/preview | 预览文件 |
| DELETE | /api/files/:id | 删除文件 |

### 分享接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /api/shares | 创建分享 |
| POST | /api/shares/:shareCode/access | 访问分享 |
| GET | /api/shares | 获取分享列表 |
| GET | /api/shares/:id | 获取分享详情 |
| POST | /api/shares/:id/revoke | 撤销分享 |

## 项目结构

```
.
├── client/                 # 前端项目
│   ├── src/
│   │   ├── api/          # API接口
│   │   ├── router/       # 路由配置
│   │   └── views/        # 页面组件
│   └── package.json
├── server/                 # 后端项目
│   ├── src/
│   │   ├── config/       # 配置文件
│   │   ├── controllers/  # 控制器
│   │   ├── models/       # 数据模型
│   │   ├── routes/       # 路由
│   │   ├── tasks/        # 定时任务
│   │   ├── utils/        # 工具类
│   │   └── app.js        # 入口文件
│   ├── uploads/          # 加密文件存储目录
│   └── package.json
└── README.md
```

## 权限说明

| 权限级别 | 说明 |
|----------|------|
| read | 仅可预览文件 |
| download | 可预览和下载文件 |
| admin | 完全控制权限 |

## 定时任务

系统会每小时自动执行一次清理任务，处理过期的文件和分享记录。

## License

MIT
