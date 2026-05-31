# WebAuthn 无密码认证系统

一个完整的无密码认证系统，使用 WebAuthn 协议实现基于生物识别（指纹/人脸）或安全密钥（YubiKey）的身份验证，集成了基于风险的自适应认证（RBA）和凭证云同步备份恢复功能。

## 特性

### 核心功能
- 🔐 **无密码登录** - 使用生物识别或安全密钥，无需记忆密码
- 📱 **多设备支持** - 每个账户最多可绑定 5 台设备
- 🔒 **安全密钥保护** - 私钥永不离开用户设备
- 🎫 **JWT 认证** - 登录后签发 JWT Token 用于后续请求认证
- 📋 **设备管理** - 查看已绑定设备列表和最后使用时间
- 🔄 **设备解绑** - 随时解绑不再使用的设备

### 凭证备份与恢复
- ☁️ **云同步备份** - 凭证ID和公钥使用密码派生密钥加密后存储在服务端
- 🔑 **恢复码** - 生成一次性恢复码，30天有效期
- 📧 **邮箱/短信验证** - 支持邮箱OTP或短信验证码身份验证
- 🔐 **OIDC集成** - 企业SSO场景下通过OIDC身份提供者恢复

### 基于风险的自适应认证 (RBA)
- 🛡️ **风险评估** - 收集IP地理位置、设备指纹、时间段、用户行为模式
- 🌲 **孤立森林算法** - 使用Isolation Forest计算风险分数(0-100)
- ⚠️ **分级响应**:
  - **低风险 (<30分)** - 直接通过
  - **中风险 (30-60分)** - 要求WebAuthn+辅助手势
  - **高风险 (60-85分)** - 要求邮箱OTP或管理员审批
  - **极高风险 (>85分)** - 账号锁定或强制人工审核
- 🤖 **自动训练** - 模型每7天自动重新训练
- 📡 **威胁情报** - 集成外部威胁情报服务查询IP信誉

## 技术栈

### 后端
- Go 1.21+
- [go-webauthn](https://github.com/go-webauthn/webauthn) - WebAuthn 库
- Gorilla Mux - HTTP 路由
- JWT - Token 认证
- Isolation Forest - 异常检测算法
- AES-256-GCM - 加密备份数据

### 前端
- React 18+
- React Router - 路由管理
- WebAuthn API - 浏览器原生认证接口

## 项目结构

```
.
├── server/                 # Go 后端
│   ├── cmd/
│   │   └── main.go        # 入口文件
│   ├── internal/
│   │   ├── models/        # 数据模型
│   │   ├── handlers/      # HTTP 处理器
│   │   ├── middleware/    # 中间件（JWT认证）
│   │   ├── store/         # 数据存储
│   │   ├── crypto/        # 加密工具
│   │   ├── risk/          # 风险评估引擎
│   │   └── threatintel/   # 威胁情报客户端
│   ├── go.mod
│   └── .env.example
└── client/                # React 前端
    ├── src/
    │   ├── api/           # API 和 WebAuthn 封装
    │   ├── components/    # React 组件
    │   ├── hooks/         # 自定义 Hooks
    │   ├── pages/         # 页面组件
    │   ├── App.jsx        # 主应用
    │   └── main.jsx       # 入口
    └── package.json
```

## 快速开始

### 环境要求

- Go 1.21+
- Node.js 18+
- 支持 WebAuthn 的浏览器（Chrome、Firefox、Edge、Safari）

### 1. 启动后端

```bash
cd server

# 复制配置文件
cp .env.example .env

# 安装依赖
go mod download

# 启动服务
go run cmd/main.go
```

后端将在 `http://localhost:8080` 启动。

### 2. 启动前端

```bash
cd client

# 安装依赖
npm install

# 启动开发服务器
npm run dev
```

前端将在 `http://localhost:3000` 启动。

## 使用流程

### 注册流程

1. 访问注册页面 `/register`
2. 输入用户名、显示名称和设备名称
3. 点击"注册并绑定设备"
4. 浏览器弹出认证对话框，使用指纹/人脸或安全密钥进行验证
5. 验证成功后自动登录

### 登录流程

1. 访问登录页面 `/login`
2. 输入用户名
3. 点击"使用通行密钥登录"
4. 系统自动评估风险等级
5. 浏览器弹出认证对话框，使用已注册的方式进行验证
6. 如为高风险登录，需完成额外验证（邮箱OTP等）
7. 验证成功后自动跳转至主页

### 设备管理

1. 登录后进入主页
2. 查看已绑定设备列表
3. 点击设备的"解绑"按钮
4. 确认解绑操作

### 创建凭证备份

1. 登录后调用 `POST /api/recovery/backup`
2. 设置备份密码（用于加密凭证数据）
3. 系统生成恢复码，请妥善保存
4. 凭证数据使用密码派生的AES-256密钥加密存储

### 账户恢复

1. 访问恢复页面 `/recovery`
2. 输入用户名和恢复码
3. 验证成功后输入备份密码解密凭证
4. 执行WebAuthn设备绑定流程
5. 重新获得账户访问权限

### 风险评估管理

- 查看风险统计: `GET /api/risk/stats`
- 手动触发模型重训练: `POST /api/risk/retrain`
- 启用/禁用风险引擎: `PUT /api/risk/config`
- 查看用户风险历史: `GET /api/risk/history`

## API 接口

### 认证接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/register/begin` | 开始注册流程 |
| POST | `/api/register/finish` | 完成注册流程 |
| POST | `/api/login/begin` | 开始登录流程（含风险评估） |
| POST | `/api/login/finish` | 完成登录流程 |

### 凭证备份与恢复接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/recovery/backup` | 创建凭证备份（需认证） |
| POST | `/api/recovery/decrypt` | 解密备份数据（需认证） |
| POST | `/api/recovery/send-code` | 发送验证邮件/短信 |
| POST | `/api/recovery/verify-code` | 验证验证码 |
| POST | `/api/recovery/verify-recovery-code` | 验证恢复码 |
| POST | `/api/recovery/begin` | 开始恢复流程（需恢复Token） |
| POST | `/api/recovery/finish` | 完成恢复并绑定设备 |
| POST | `/api/recovery/oidc` | 通过OIDC身份验证 |

### 风险评估接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/risk/check` | 检查登录风险 |
| GET | `/api/risk/stats` | 获取风险引擎统计 |
| POST | `/api/risk/retrain` | 重新训练模型 |
| GET | `/api/risk/history` | 获取用户风险历史（需认证） |
| PUT | `/api/risk/config` | 更新风险配置 |

### 用户接口（需要 JWT 认证）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/user/profile` | 获取用户信息 |
| GET | `/api/user/credentials` | 获取设备列表 |
| DELETE | `/api/user/credentials/{id}` | 解绑设备 |

## 安全说明

- 私钥存储在用户设备的安全元件中，永不离开设备
- 服务端仅存储公钥用于验证签名
- 凭证备份使用AES-256-GCM加密，密钥由用户密码通过PBKDF2派生
- 恢复码一次性使用，30天有效期
- 使用 HTTPS 协议传输数据
- JWT Token 有效期为 24 小时
- 恢复Token 有效期为 15 分钟

## 风险分级说明

| 风险等级 | 分数范围 | 响应动作 |
|---------|---------|---------|
| 低风险 | 0-30 | 直接通过 |
| 中风险 | 30-60 | 要求辅助手势验证 |
| 高风险 | 60-85 | 要求邮箱/短信OTP |
| 极高风险 | 85-100 | 账号锁定/人工审核 |

## 配置说明

### 环境变量

```env
# 基础配置
PORT=8080
RP_ID=localhost
RP_ORIGIN=http://localhost:3000
RP_DISPLAY_NAME=WebAuthn Demo
JWT_SECRET=your-secret-key

# 风险引擎配置
RISK_ENABLED=true

# 威胁情报服务（可选）
THREAT_INTEL_API=https://api.threatintel.com
THREAT_INTEL_KEY=your-api-key

# 邮件服务配置
EMAIL_PROVIDER=smtp
EMAIL_HOST=smtp.example.com
EMAIL_PORT=587
EMAIL_USER=noreply@example.com
EMAIL_PASSWORD=your-password

# 短信服务配置
SMS_PROVIDER=twilio
SMS_API_KEY=your-api-key
SMS_SENDER=+1234567890

# OIDC 配置
OIDC_PROVIDERS=google,microsoft
OIDC_CLIENT_ID=your-client-id
OIDC_CLIENT_SECRET=your-client-secret
```

## 注意事项

- WebAuthn 需要 HTTPS 环境（localhost 除外）
- 生产环境需要配置正确的 RP ID 和 RP Origin
- 建议将 JWT_SECRET 设置为强随机字符串
- 当前使用内存存储，生产环境应使用数据库
- 风险模型需要足够的登录数据（至少10条）才能有效训练
- 备份密码一旦丢失无法恢复，请妥善保管

## 许可证

MIT
