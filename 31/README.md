# 分布式配置中心 Config Center

基于 **Java Spring Boot + etcd** 的分布式配置中心，支持多环境（dev/staging/prod）、多应用，配置以 **YAML** 格式存储。
客户端通过 **gRPC 长连接**订阅配置变更，服务端主动推送到所有订阅客户端；配置每次修改都会生成递增版本号并记录变更历史，支持**回滚到任意历史版本**。

## 模块结构

```
31/
├── config-center-proto/       gRPC/protobuf 定义 (共享契约)
├── config-center-server/      服务端 (Spring Boot + etcd)
│   ├── gRPC 服务 (端口 9090)
│   └── REST 管理 API (端口 8080)
├── config-center-client/      Java 客户端 SDK
├── config-center-cli/         命令行工具 (picocli + shade fat jar)
├── config-center-web/         Web 管理界面 (Vue3 + Element Plus)
└── docker-compose.yaml        启动 etcd
```

## 核心能力

| 能力 | 说明 |
| --- | --- |
| 多环境 | `dev` / `staging` / `prod` |
| 多应用 | 通过 application 名称隔离，支持无限扩展 |
| YAML 存储 | 配置内容以原生 YAML 格式存储，写入时做语法校验 |
| 版本控制 | 每次修改自动递增整数版本号 |
| 变更历史 | 记录谁、什么时候、改了什么 (版本 diff) |
| gRPC 订阅 | `WatchConfig` 服务端流式推送，断线自动重连 |
| 回滚 | 一键回滚到任意历史版本，并产生新的版本号 |
| **灰度发布** | 按 `abs(hash(ip+instanceId)) % 100 < percent` 圈选灰度客户端 |
| **观察期自动决策** | 观察期内实时监控错误率，到期自动全量或超阈值自动取消 |
| **客户端心跳** | 每 30s 上报实例 ID / IP / 版本 / 健康状态 |
| **CLI** | 命令行 gray / publish / get / watch / history / rollback / list |
| **Web** | Vue3 + Element Plus 灰度监控面板：实时指标 / 客户端列表 / 进度条 |

## 架构

```
         ┌──────────────┐
         │  Web (Vue3)  │──REST──┐
         └──────────────┘        │
                                 ▼
         ┌──────────────┐   ┌─────────┐
  CLI ──┼── gRPC ───┐   ├──►│  Server │◄── etcd 事务写
         └──────────────┘   │  (Spring)│
                            └────┬────┘
                                 │ 推送变更
                                 ▼
                     ┌──────────────────┐
                     │ Java Client SDK  │  (WatchManager)
                     │ (gRPC streaming) │
                     └──────────────────┘
```

### etcd key 布局

```
/config-center/apps                          (应用索引 CSV)
/config-center/current/{app}/{profile}/{key} (当前版本 JSON)
/config-center/history/{app}/{profile}/{key}/{version} (历史版本 JSON)
```

写入使用 `etcd Txn` 基于 `mod_revision` 做 CAS，保证版本号递增无冲突；写入成功后由 `WatchManager` 广播至所有订阅者。

## 快速启动

### 1. 启动 etcd

```bash
docker compose up -d
```

### 2. 编译 (首次需要生成 protobuf 代码)

```bash
mvn clean package -DskipTests
```

### 3. 启动服务端

```bash
cd config-center-server
mvn spring-boot:run
# HTTP:  http://localhost:8080
# gRPC:  localhost:9090
```

可通过 `application.yaml` 修改 etcd 地址：

```yaml
config-center:
  etcd:
    endpoints: http://localhost:2379
    prefix: /config-center
  grpc:
    host: 0.0.0.0
    port: 9090
```

### 4. 启动 Web 管理界面

```bash
cd config-center-web
npm install
npm run dev   # http://localhost:5173
```

### 5. 构建 CLI

```bash
cd config-center-cli
mvn package -DskipTests
# 生成 target/config-center-cli.jar (fat jar)
java -jar target/config-center-cli.jar --help
```

## REST API 速览

| Method | Path | 说明 |
| --- | --- | --- |
| GET  | `/api/v1/apps` | 列出所有应用 |
| GET  | `/api/v1/apps/{app}/profiles` | 列出环境 |
| GET  | `/api/v1/apps/{app}/profiles/{p}/keys` | 列出配置键 (含当前版本) |
| GET  | `/api/v1/config?application=X&profile=Y&key=Z` | 读取当前配置 |
| POST | `/api/v1/config` | 发布 (body: `{application, profile, key, content, operator}`) |
| DELETE | `/api/v1/config?application=X&profile=Y&key=Z` | 删除 |
| GET  | `/api/v1/config/history?application=X&profile=Y&key=Z` | 查看历史 |
| GET  | `/api/v1/config/history/{version}?application=X&profile=Y&key=Z` | 查看某版本 |
| POST | `/api/v1/config/rollback` | 回滚 (body: `{application, profile, key, targetVersion, operator}`) |

## CLI 使用

```bash
# 查看所有应用
java -jar config-center-cli.jar list apps

# 发布配置
java -jar config-center-cli.jar publish -a order-svc -e dev -k database -f ./database.yaml -o alice

# 读取当前配置
java -jar config-center-cli.jar get -a order-svc -e dev -k database

# 查看历史
java -jar config-center-cli.jar history -a order-svc -e dev -k database -n 10

# 回滚到 v3
java -jar config-center-cli.jar rollback -a order-svc -e dev -k database -v 3 -o alice

# 订阅变更 (长连接，断线自动重连)
java -jar config-center-cli.jar watch -a order-svc -e dev -k database
```

## Java 客户端 SDK

```xml
<dependency>
  <groupId>com.configcenter</groupId>
  <artifactId>config-center-client</artifactId>
  <version>1.0.0</version>
</dependency>
```

```java
ConfigCenterClient client = new ConfigCenterClient("localhost", 9090);

// 读取
ConfigEntry e = client.getConfig("order-svc", "dev", "database");
System.out.println(e.getContent());

// 订阅 (长连接)
client.watch("order-svc", "dev", "database", 0L, resp -> {
    System.out.println("v" + resp.getEntry().getVersion() + ": " + resp.getEventType());
    System.out.println(resp.getEntry().getContent());
});
```

## 技术栈

- **Java 17**, Spring Boot 3.2
- **gRPC 1.62** (protobuf-maven-plugin 代码生成)
- **etcd jetcd 0.7.10** (Txn CAS 保证并发安全)
- **SnakeYAML 2** (YAML 校验/解析)
- **Picocli 4** (CLI)
- **Vue 3 + Element Plus + Vite** (Web)

## 后续可扩展

- 配置内容差异对比 (接入 `diffutils`)
- 配置命名空间 / RBAC 权限
- etcd 多节点集群 + TLS
- 灰度发布 / 按 client 标签过滤推送
- WebSocket 替代 gRPC 实现浏览器端订阅
