# PolarDB Global Index Sync Tool

PolarDB分布式数据库全局索引同步工具，基于Canal实现binlog实时监听，维护全局索引表以支持按全局唯一键快速定位分片。

## 功能特性

- ✅ **多Shard binlog监听**：支持同时监听多个PolarDB分片的binlog变更
- ✅ **全局索引维护**：实时同步insert/update/delete事件到中心全局索引表
- ✅ **高性能**：支持TPS 1万+高并发写入，查询延迟<5ms
- ✅ **全量重建**：支持`--rebuild`参数从分片全量重建全局索引
- ✅ **断点续传**：基于ZooKeeper记录binlog消费位置，异常恢复后自动续传
- ✅ **同步监控**：Prometheus指标暴露同步延迟、TPS、成功率等关键指标
- ✅ **HTTP API**：提供RESTful API查询全局ID对应的分片位置
- ✅ **高可用**：支持Canal集群模式、连接池、异步批处理优化

## 架构设计

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   PolarDB Shard0│────▶│    Canal Server │────▶│                 │
└─────────────────┘     └─────────────────┘     │                 │
┌─────────────────┐     ┌─────────────────┐     │  Global Index   │
│   PolarDB Shard1│────▶│    Canal Server │────▶│  Sync Tool      │─────▶ Center DB
└─────────────────┘     └─────────────────┘     │                 │       │
┌─────────────────┐     ┌─────────────────┐     │  (Java + Canal) │       │
│   PolarDB Shard2│────▶│    Canal Server │────▶│                 │       │
└─────────────────┘     └─────────────────┘     └─────────────────┘       │
       │                                              │                    │
       │                                              ▼                    ▼
       │                                    ZooKeeper (Position)    Global Index Table
       │
       └───── HTTP API: GET /api/v1/global-index/query/{globalId}
```

## 技术栈

- **Java 8**
- **Spring Boot 2.7.x**
- **Canal Client 1.1.7** - binlog订阅客户端
- **Apache Curator** - ZooKeeper客户端
- **HikariCP** - 数据库连接池
- **Micrometer + Prometheus** - 监控指标
- **Guava** - 本地缓存、工具类

## 快速开始

### 1. 环境要求

- JDK 1.8+
- Maven 3.6+
- Canal Server 1.1.4+
- ZooKeeper 3.6+
- PolarDB for MySQL 8.0+

### 2. 配置说明

修改 `src/main/resources/application.yml`:

```yaml
global-index:
  sync:
    zookeeper:
      servers: 127.0.0.1:2181
    center-db:
      url: jdbc:polardb://127.0.0.1:3306/global_index_db
      username: root
      password: root
    shards:
      - id: shard0
        name: PolarDB分片0
        canal-server: 127.0.0.1:11111
        destination: polardb_shard0
        db-url: jdbc:polardb://127.0.0.1:3307/shard_db
        shard-key: user_id
        global-id-column: order_id
        source-table: shard_db.t_order
```

### 3. 编译打包

```bash
mvn clean package -DskipTests
```

### 4. 运行模式

#### 4.1 增量同步模式（默认）

```bash
java -jar polardb-global-index-sync-1.0.0.jar
```

#### 4.2 全量重建模式

```bash
# 重建所有分片
java -jar polardb-global-index-sync-1.0.0.jar --rebuild

# 重建指定分片
java -jar polardb-global-index-sync-1.0.0.jar --rebuild-shard shard0
```

## HTTP API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/global-index/query/{globalId}` | 查询单个全局ID的分片位置 |
| GET | `/api/v1/global-index/query/batch?globalIds=id1,id2` | 批量查询 |
| GET | `/api/v1/global-index/shard/{shardKey}` | 按分片键查询 |
| POST | `/api/v1/global-index/rebuild` | 触发全量重建 |
| POST | `/api/v1/global-index/rebuild?shardId=shard0` | 重建指定分片 |
| GET | `/api/v1/global-index/status` | 获取同步状态 |
| POST | `/api/v1/global-index/listener/{shardId}/start` | 启动监听器 |
| POST | `/api/v1/global-index/listener/{shardId}/stop` | 停止监听器 |
| GET | `/api/v1/global-index/position/{shardId}` | 获取binlog消费位置 |

## 监控指标

通过 `http://localhost:8080/actuator/prometheus` 访问：

| 指标名 | 说明 |
|--------|------|
| `global_index_sync_success_total` | 同步成功总数 |
| `global_index_sync_failed_total` | 同步失败总数 |
| `global_index_sync_delay_ms{shard="shard0"}` | 分片同步延迟(ms) |
| `global_index_process_duration_ms_seconds` | 处理耗时分布 |
| `global_index_total_count` | 全局索引总记录数 |
| `global_index_tps_estimate` | 估算TPS |

## 性能优化

1. **批量写入**：使用`rewriteBatchedStatements=true` + `ON DUPLICATE KEY UPDATE`实现高性能upsert
2. **连接池优化**：HikariCP配置64连接，支持高并发
3. **本地缓存**：Guava Cache缓存热点数据，降低DB查询压力
4. **异步处理**：32线程异步处理binlog事件，事件队列缓冲
5. **批处理**：默认512条批量写入，支持100ms自动刷盘
6. **主键索引**：global_id为主键，查询O(1)复杂度

## 核心类说明

| 类名 | 说明 |
|------|------|
| `GlobalIndexSyncApplication` | 主启动类，支持--rebuild参数 |
| `MultiShardCanalListener` | 多分片Canal监听器，每个分片独立线程 |
| `BinlogEventParser` | binlog事件解析器，提取全局ID和分片键 |
| `DataChangeProcessor` | 事件处理器，异步批量写入索引表 |
| `ZkPositionManager` | ZooKeeper位置管理器，断点续传 |
| `GlobalIndexDaoImpl` | 全局索引DAO，高性能CRUD实现 |
| `FullRebuildService` | 全量重建服务，多分片并行扫描 |
| `SyncMetrics` | Prometheus监控指标收集 |
| `GlobalIndexController` | HTTP API控制器 |

## 数据库表结构

### 全局索引表 (中心库)

```sql
CREATE TABLE t_global_index (
  global_id VARCHAR(128) NOT NULL COMMENT '全局唯一ID',
  shard_key VARCHAR(128) NOT NULL COMMENT '分片键',
  shard_id VARCHAR(64) NOT NULL COMMENT '分片ID',
  gmt_create DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  gmt_modified DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (global_id),
  KEY idx_shard_key (shard_key),
  KEY idx_shard_id (shard_id)
) ENGINE=InnoDB;
```

## License

Apache 2.0
