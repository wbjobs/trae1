# CDC Sync Tool

数据库CDC（Change Data Capture）同步工具，基于Java + Debezium + Kafka构建，支持MySQL和PostgreSQL的binlog/WAL变更捕获。

## 功能特性

- ✅ **多数据库支持**：MySQL（binlog）、PostgreSQL（WAL/Logical Decoding）
- ✅ **全量快照 + 增量同步**：支持initial、initial_only、never等多种快照模式
- ✅ **Kafka集成**：每个表对应一个topic，支持自定义topic名称
- ✅ **Schema Registry + Avro**：使用Confluent Schema Registry管理Avro schema，支持向后兼容
- ✅ **表过滤和字段裁剪**：支持单表过滤，可指定包含/排除字段
- ✅ **Prometheus监控**：暴露同步延迟（lag秒数）、事件处理速率等metrics
- ✅ **CLI校验工具**：提供数据一致性校验（行数对比+抽样校验）

## 项目结构

```
cdc-sync-parent/
├── cdc-common/          # 公共模块：配置类、事件模型、工具类
├── cdc-core/            # 核心模块：Debezium引擎、事件分发
├── cdc-serde/           # 序列化模块：Avro序列化、Schema Registry集成
├── cdc-metrics/         # 监控模块：Prometheus指标暴露
├── cdc-cli/             # CLI工具：数据一致性校验
└── config/              # 配置示例
```

## 快速开始

### 环境要求

- JDK 11+
- Maven 3.6+
- Kafka 2.8+
- Confluent Schema Registry 6.2+
- MySQL 5.7+ 或 PostgreSQL 10+

### 构建项目

```bash
mvn clean package -DskipTests
```

### 启动CDC同步

**MySQL配置**：
```bash
java -jar cdc-core/target/cdc-core-1.0.0.jar config/mysql-config.yaml
```

**PostgreSQL配置**：
```bash
java -jar cdc-core/target/cdc-core-1.0.0.jar config/postgresql-config.yaml
```

### 配置说明

#### MySQL配置要求

1. 启用binlog：
```ini
[mysqld]
server-id = 1
log_bin = mysql-bin
binlog_format = ROW
binlog_row_image = FULL
```

2. 用户权限：
```sql
GRANT SELECT, RELOAD, SHOW DATABASES, REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'cdc'@'%';
```

#### PostgreSQL配置要求

1. 启用逻辑复制：
```ini
wal_level = logical
max_replication_slots = 4
max_wal_senders = 4
```

2. 创建复制槽：
```sql
SELECT pg_create_logical_replication_slot('cdc_slot', 'pgoutput');
CREATE PUBLICATION cdc_publication FOR ALL TABLES;
```

## 表过滤和字段裁剪

支持按表过滤，并可指定包含或排除的字段：

```yaml
tableFilters:
  - tableName: mydb.users
    includedColumns:
      - id
      - name
      - email
    topicName: cdc.users

  - tableName: mydb.orders
    excludedColumns:
      - sensitive_data
      - internal_notes
```

## Schema兼容性

支持以下兼容性级别：
- `backward`（默认）：向后兼容
- `backward_transitive`：传递向后兼容
- `forward`：向前兼容
- `forward_transitive`：传递向前兼容
- `full`：完全兼容
- `full_transitive`：传递完全兼容
- `none`：不检查

## Prometheus监控

Metrics默认暴露在 `http://localhost:9090/metrics`

关键指标：

| 指标名称 | 类型 | 描述 |
|---------|------|------|
| `cdc_events_total` | Counter | 处理的事件总数 |
| `cdc_events_failed_total` | Counter | 失败的事件数 |
| `cdc_event_lag_seconds` | Gauge | 事件延迟（秒） |
| `cdc_last_event_timestamp_seconds` | Gauge | 最后处理事件的时间戳 |
| `cdc_snapshot_progress_percent` | Gauge | 快照进度百分比 |
| `cdc_event_processing_latency_seconds` | Histogram | 事件处理延迟分布 |

**Grafana查询示例**：
```promql
# 表级别的同步延迟
cdc_event_lag_seconds{table="users"}

# 事件处理速率
rate(cdc_events_total[5m])
```

## CLI数据一致性校验工具

构建校验工具：
```bash
mvn package -pl cdc-cli -am -DskipTests
```

使用示例：

```bash
java -jar cdc-cli/target/cdc-cli-1.0.0.jar validate \
  --source-type mysql \
  --source-host localhost \
  --source-port 3306 \
  --source-database mydb \
  --source-username root \
  --source-password password \
  --target-type postgresql \
  --target-host localhost \
  --target-port 5432 \
  --target-database mydb \
  --target-username postgres \
  --target-password password \
  --table mydb.users \
  --columns id,name,email \
  --sample-size 1000
```

校验内容：
1. **行数对比**：对比源端和目标端的总行数
2. **抽样校验**：随机抽取N行数据，逐字段对比
3. **结果输出**：显示不匹配的字段详情

退出码：
- `0`：校验通过
- `1`：校验失败（数据不一致）
- `2`：执行错误

## Kafka Topic格式

每个表对应一个topic，默认topic名称为：`{prefix}.{schema}.{table}`

消息格式（Avro）：
```json
{
  "operation": "INSERT",
  "timestamp": 1700000000000,
  "transactionId": "xxx",
  "before": null,
  "after": {
    "id": 1,
    "name": "John",
    "email": "john@example.com"
  },
  "source": {
    "database": "mydb",
    "schema": "mydb",
    "table": "users",
    "position": "mysql-bin.000001:12345"
  }
}
```

## 快照模式

| 模式 | 描述 |
|-----|------|
| `initial` | 首次启动时执行全量快照，然后进入增量同步 |
| `initial_only` | 仅执行全量快照，完成后停止 |
| `never` | 跳过快照，直接从当前binlog/WAL位置开始 |
| `schema_only` | 仅捕获表结构变更，不捕获数据 |
| `schema_only_recovery` | 用于恢复，仅重建表结构历史 |

## 部署建议

### Docker Compose部署参考

```yaml
version: '3'
services:
  zookeeper:
    image: confluentinc/cp-zookeeper:7.5.0
    environment:
      ZOOKEEPER_CLIENT_PORT: 2181

  kafka:
    image: confluentinc/cp-kafka:7.5.0
    depends_on:
      - zookeeper
    ports:
      - "9092:9092"
    environment:
      KAFKA_ZOOKEEPER_CONNECT: zookeeper:2181
      KAFKA_ADVERTISED_LISTENERS: PLAINTEXT://kafka:29092,PLAINTEXT_HOST://localhost:9092
      KAFKA_LISTENER_SECURITY_PROTOCOL_MAP: PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT
      KAFKA_INTER_BROKER_LISTENER_NAME: PLAINTEXT

  schema-registry:
    image: confluentinc/cp-schema-registry:7.5.0
    depends_on:
      - kafka
    ports:
      - "8081:8081"
    environment:
      SCHEMA_REGISTRY_KAFKASTORE_BOOTSTRAP_SERVERS: kafka:29092
      SCHEMA_REGISTRY_HOST_NAME: schema-registry
      SCHEMA_REGISTRY_AVRO_COMPATIBILITY_LEVEL: backward
```

## 监控告警建议

建议配置以下Prometheus告警规则：

```yaml
groups:
  - name: cdc.rules
    rules:
      - alert: CDCEventLagHigh
        expr: cdc_event_lag_seconds > 60
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "CDC event lag is high ({{ $value }}s)"
          description: "Table {{ $labels.table }} has lag > 60s"

      - alert: CDCEventsFailed
        expr: increase(cdc_events_failed_total[5m]) > 10
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "CDC events are failing"
          description: "{{ $value }} events failed in last 5m"
```

## License

MIT License
