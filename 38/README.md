# SVID Gateway

基于 SPIRE (SPIFFE) 的服务身份认证与细粒度授权网关。

## 核心能力

- **SVID 签发**：通过 SPIRE Workload API 为每个服务实例签发 **X.509-SVID** 与 **JWT-SVID**
- **服务身份认证**：在 gRPC / HTTP 调用链路上校验对端 SVID 的有效性
- **细粒度授权策略**：支持定义 `source SPIFFE ID` → `destination SPIFFE ID` × `method` × `path` 的 allow/deny 规则
- **策略热加载**：策略存储在 etcd，通过 Watch 机制实时生效
- **审计日志**：所有策略变更通过 etcd 追加写入审计条目
- **Web 管理界面**：基于 React + Ant Design 的可视化配置，含策略评估器
- **证书自动轮换**：剩余有效期低于阈值（默认 30%）时触发轮换
- **CLI 工具**：`svidctl` 列出所有注册的服务身份与证书有效期

## 目录结构

```
cmd/
  svid-gateway/     # 网关主程序（管理API + Web UI）
  svidctl/          # CLI 工具
internal/
  admin/            # 管理 API（策略、身份、SVID、审计）
  audit/            # 审计日志模块
  config/           # 配置加载
  gateway/          # HTTP 中间件 / gRPC 拦截器
  identity/         # SPIRE Workload API 封装（X.509 / JWT SVID）
  policy/           # 策略定义与策略引擎
  registry/         # 服务身份注册（etcd）
  store/            # etcd 策略存储与审计存储
web/                # React + Vite 管理界面
examples/           # HTTP / gRPC 服务端示例
deploy/             # SPIRE Server / Agent 配置
```

## 快速开始

```bash
# 1. 启动依赖
docker compose up -d etcd spire-server spire-agent

# 2. 注册 SPIFFE 条目
./deploy/spire/register.sh

# 3. 构建 Web 与网关
cd web && npm install && npm run build && cd ..
go build -o svid-gateway ./cmd/svid-gateway
go build -o svidctl ./cmd/svidctl

# 4. 启动网关
./svid-gateway

# 5. 打开管理界面
open http://localhost:8080/ui/
```

## 策略格式

```json
{
  "id": "pol-user-api-allow",
  "name": "Allow client to call User API",
  "source": "spiffe://example.org/ns/svc/client",
  "destination": "spiffe://example.org/ns/svc/user-api",
  "methods": ["POST", "GET"],
  "path": "/api/v1/users",
  "path_type": "prefix",
  "effect": "allow",
  "priority": 100,
  "enabled": true
}
```

- `source` / `destination` 支持 `spiffe://...` 精确匹配或 `*` 通配
- `path_type` 可选 `exact` / `prefix` / `regex`
- 按 `priority` 从高到低匹配，命中即返回；无命中默认 deny

## 在服务中集成

### HTTP (Gin)

```go
jwtV, _ := identity.NewJWTValidator(ctx, "/tmp/spire-agent/public/api.sock")
gw := gateway.New(jwtV, engine, trustRoots)
router.Use(gw.HTTPMiddleware("spiffe://example.org/ns/svc/your-svc"))
```

### gRPC

```go
grpc.NewServer(
  grpc.UnaryInterceptor(gw.GRPCUnaryInterceptor("spiffe://example.org/ns/svc/your-svc")),
  grpc.StreamInterceptor(gw.GRPCStreamInterceptor("spiffe://example.org/ns/svc/your-svc")),
)
```

## CLI

```bash
# 列出所有授权策略
./svidctl --etcd=localhost:2379 policy list

# 列出所有已注册的服务身份
./svidctl --etcd=localhost:2379 identity list
```

## 审计

审计日志写入 etcd key `/svid-gateway/audit/<timestamp>`，可通过管理界面 `Audit Log` 查看，或：

```bash
ETCDCTL_API=3 etcdctl get --prefix /svid-gateway/audit
```

## 配置环境变量

| 变量 | 默认 | 说明 |
| --- | --- | --- |
| `ADMIN_PORT` | `8080` | 管理 API / Web UI 端口 |
| `GATEWAY_PORT` | `8443` | 代理/网关端口（保留） |
| `ETCD_ENDPOINTS` | `localhost:2379` | etcd 集群地址（逗号分隔） |
| `SPIRE_SOCKET_PATH` | `/tmp/spire-agent/public/api.sock` | SPIRE Agent Workload API socket |
| `ROTATE_THRESHOLD` | `0.30` | 剩余有效期低于此比例时触发轮换 |
| `ROTATE_CHECK_PERIOD` | `1h` | 轮换检查周期 |
| `SVID_CACHE_DIR` | `./data/svid-cache` | SVID 磁盘缓存目录 |
| `SVID_GRACE_PERIOD` | `24h` | SVID 过期后宽限期 |
| `TRUST_DOMAIN` | `example.org` | SPIFFE 信任域 |
