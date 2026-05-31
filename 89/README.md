# Arrow Columnar Data Transfer Gateway

基于 **Apache Arrow 列式格式 + gRPC + DataFusion** 的数据传输网关，支持 **Python / Java / Go** 三种语言的客户端上传和下载表格数据（最大 10GB+）。

## 核心特性

- **列式格式存储与传输**：内部全程使用 Apache Arrow 的列式内存布局。
- **零拷贝路径**：
  - Python pandas DataFrame → pyarrow Table → Arrow IPC（File 格式）发送；
  - Rust 网关直接按 Arrow IPC File 存储（磁盘）；
  - 下载时以 Arrow IPC Stream 分片返回；
  - Go 客户端直接把 IPC 字节还原为 Go arrow.Array；
  - Java 客户端使用 Arrow VectorSchemaRoot，列块按原序对齐。
- **字典编码自动优化**（解决大字符串列 OOM 问题）：
  - 自动检测低基数字符串列（去重后 < 总行数 10%）；
  - 自动转换为 `Dictionary<Int32, Utf8>`，内存可减少 70%+；
  - 典型场景：1000 万行 × 100 字节字符串列，从 20GB+ → <3GB；
  - 通过 `--optimize-schema` 开关启用。
- **双层压缩**：
  - **Arrow IPC 层 LZ4 压缩**：使用 Arrow 原生 `CompressionCodec::LZ4_FRAME`，
    对列式数据本身做压缩，Python/Java/Go 客户端均可透明解压；
  - **gRPC 层 gzip 压缩**：传输层进一步压缩，与 Arrow 压缩叠加；
  - 通过 `--compression lz4|zstd|none` 控制。
- **数据查询（投影 + 过滤）**：
  - 通过 DataFusion SQL 在 Arrow 列式格式上直接执行；
  - 示例：`SELECT col1, col2 FROM t WHERE col3 > 10`；
  - 也支持省略 `FROM t` 的简写：`SELECT col1, col2 WHERE col3 > 10`。
- **版本管理**：每个数据集保留最多 `--max-versions` 个历史版本（默认 10）。

## 架构

```
Python(pandas)  ╲                    ╱  Go(arrow.Array)
Java(VSchema)    ──► Arrow IPC ──► Rust Gateway ──► Arrow IPC ──► Python/Java/Go
Go(arrow.Table) ╱         (File/Stream)               (Stream)
                                  │
                                  └──► DataFusion SQL ──► Arrow Table
```

- **上传**：客户端把 Table/VSchema/RecordBatch 序列化为 Arrow IPC **File 格式**（含 schema 与所有 batch），分片后通过 gRPC 流式发给网关。
- **存储**：网关以 Arrow IPC File 格式直接落盘到 `{data_dir}/{dataset}/v{version}.arrow`，不做二次解析或转换。
- **下载**：网关以 Arrow IPC **Stream 格式**按 batch 分片返回。
- **查询**：网关用 FileReader 把指定版本的 batches 载入，注册为 DataFusion 内存表 `t`，执行 SQL 后仍以 Arrow IPC Stream 返回。

## 协议

见 [`proto/gateway.proto`](proto/gateway.proto)。核心 RPC：

| RPC | 方向 | 说明 |
| --- | ---- | ---- |
| `Upload(stream UploadRequest) returns UploadResponse` | 客户端流 | Arrow IPC File（分片）|
| `Download(DownloadRequest) returns stream DownloadResponse` | 服务端流 | Arrow IPC Stream（分片）|
| `Query(QueryRequest) returns stream QueryResponse` | 服务端流 | Arrow IPC Stream（查询结果）|
| `ListDatasets / ListVersions / Delete` | 一元 | 元数据 |

## 运行网关（Rust）

```bash
cd .
cargo run --release -- \
  --addr [::1]:50051 \
  --data-dir ./data \
  --max-versions 10 \
  --optimize-schema \
  --compression lz4 \
  --dict-cardinality-threshold 0.10
```

参数：
- `--addr`：监听地址
- `--data-dir`：数据目录（自动创建）
- `--max-versions`：每个数据集最多保留的版本数
- `--batch-rows`：提示用的 batch 行数（用于记录/分片建议）
- `--optimize-schema`：启用字典编码自动优化
- `--dict-cardinality-threshold`：字典编码基数阈值（默认 0.10 = 10%）
- `--compression`：Arrow IPC 压缩方式：`lz4`（默认）/ `zstd` / `none`
- `--grpc-max-message-size`：gRPC 单消息最大字节（默认 2GB）

## 优化报告

当 `--optimize-schema` 启用时，每次 `Upload` 响应都会返回 `SchemaOptimization` 报告，
包含每一列的优化前后对比：

```protobuf
message SchemaOptimization {
  repeated ColumnOptimization columns = 1;
  string compression = 2;               // "lz4" / "zstd" / "none"
  int64 total_original_bytes = 3;
  int64 total_optimized_bytes = 4;
  double total_saving_ratio = 5;        // 0.0 ~ 1.0
}

message ColumnOptimization {
  string column_name = 1;
  string original_type = 2;             // e.g. "Utf8"
  string optimized_type = 3;            // e.g. "Dictionary<Int32, Utf8>"
  string encoding = 4;                  // "dictionary" / "none"
  double memory_saving_ratio = 5;       // 0.0 ~ 1.0
  int64 original_bytes = 6;
  int64 optimized_bytes = 7;
}
```

客户端可以解析该报告了解内存节省情况。

## Python 客户端

```bash
cd clients/python
pip install -e .
python -m columnar_client._codegen   # 生成 gateway_pb2 / gateway_pb2_grpc
```

使用：

```python
import pandas as pd
from columnar_client import ColumnarGatewayClient

df = pd.DataFrame({"a": [1, 2, 3], "b": [1.1, 2.2, 3.3]})

with ColumnarGatewayClient("localhost:50051") as c:
    resp = c.upload_dataframe("demo", df)
    print("Version:", resp.info.version)
    if resp.optimization:
        print("Total saving:", resp.optimization.total_saving_ratio)
        for col in resp.optimization.columns:
            if col.encoding == "dictionary":
                print(f"  Column {col.column_name}: "
                      f"{col.original_type} → {col.optimized_type}, "
                      f"saving {col.memory_saving_ratio:.0%}")

    out = c.query_as_dataframe("demo", "SELECT a, b WHERE a > 1")
    print(out)

    print(c.list_datasets())
    print(c.list_versions("demo"))
```

## Go 客户端

```bash
cd clients/go
./scripts/generate_proto.sh   # 需要 protoc + protoc-gen-go + protoc-gen-go-grpc
go run ./cmd/example
```

使用：

```go
c, _ := columnar.New(ctx, "localhost:50051")
defer c.Close()

// 上传 arrow.Table
info, _ := c.Upload(ctx, "demo", tbl)
fmt.Println(info)

// 查询（返回 arrow.Table，零拷贝获取各列 []arrow.Array）
qtbl, _ := c.Query(ctx, "demo", "SELECT col1, col2 WHERE col3 > 10", 0)
defer qtbl.Release()
col := qtbl.Column(0) // *arrow.Column，底层就是列式切片
```

## Java 客户端

```bash
cd clients/java
mvn -q compile    # 首次构建会自动用 protobuf-maven-plugin 生成 gRPC 代码
```

```java
try (var client = new ColumnarGatewayClient("localhost", 50051);
     var root = VectorSchemaRoot.create(schema, client.allocator())) {
    // 填充 root
    DatasetInfo info = client.upload("demo", root);

    VectorSchemaRoot qr = client.query(
        "demo",
        "SELECT col1, col2 FROM t WHERE col3 > 10",
        0
    );
    // 读取 qr.getFieldVectors() 即得各列的 Arrow Vector（列式切片）
}
```

## 零拷贝 / 免序列化说明

- **Python pandas → Arrow**：`pa.Table.from_pandas(df)` 在底层对齐时直接把 pandas 的列缓冲（NumPy ndarray）零拷贝地作为 pyarrow Array 共享（零拷贝）。
- **Arrow → 网络**：Python 端通过 `ArrowFileWriter` 将 Table 序列化为 Arrow IPC File 字节，这一过程只写元数据 header，列值以原样共享（IP）。
- **网关 → 磁盘**：网关直接把 IPC File 字节原样落盘，不做任何列级解析；上传路径是流式写入（>10GB 场景也不会一次性占用内存）。
- **网络 → Go/Java**：客户端按 Arrow IPC Stream 读取并恢复成各自的列式结构（`arrow.Table` / `VectorSchemaRoot`），内存布局与原始表完全一致。
- **字典编码**：字符串列从 `Utf8`（每值 ~100 字节 + 8 字节 offset + overhead）
  转换为 `Dictionary<Int32, Utf8>`（每值 4 字节 key + 去重后的共享字符串池），
  10% 基数 → 节省 ~90% 的字符串存储，加 overhead 通常净省 >70%。

> 注：gRPC 协议层本身会有一次字节拷贝；这是 gRPC 的必要开销，无法避免，但与列式数据本身无关。列式数据的语义结构在三种语言之间保持一致。

## 查询方言

网关使用 DataFusion，表名固定为 `t`（一个数据集一个逻辑表）。
支持的示例：

```sql
SELECT col1, col2 FROM t WHERE col3 > 10
SELECT * FROM t WHERE col3 > 10 ORDER BY col1 LIMIT 100
SELECT col1, SUM(col2) FROM t GROUP BY col1
```

服务端会对不含 `FROM` 的 SQL 自动在末尾追加 `FROM t`，因此以下也是合法的：

```sql
SELECT col1, col2 WHERE col3 > 10
```

## 版本与淘汰

- 每次 `Upload` 都会创建新版本（自增 1）。
- 超过 `--max-versions`（默认 10）的最旧版本会被自动删除。
- `Delete(dataset, version=0)` 删除整个数据集；`version>0` 删除指定版本。
- `ListVersions` 按版本号倒序列出所有历史版本。

## 联合查询（Federated Query）

网关支持跨多个异构数据源的联合查询，支持 **PostgreSQL / MySQL / S3 Parquet / 本地 Parquet**。

### 核心特性

- **数据源注册**：动态注册多个数据源，在 SQL 中通过 `datasource.table` 引用。
- **谓词下推**（Predicate Pushdown）：将 WHERE 条件下推到数据源执行，减少数据传输量。
- **列裁剪**（Column Pruning）：只读取需要的列，进一步减少 I/O。
- **Join 策略自动选择**：
  - **Hash Join**：小表（< 10万行）构建哈希表，大表探针；
  - **Sort Merge Join**：大表（> 100万行）先排序再归并，内存效率更高；
  - 自动根据两侧数据量估算选择最优策略。
- **查询超时**：默认 60 秒，可通过 `timeout_seconds` 参数自定义。
- **执行计划展示**：JSON 格式的执行计划，含优化器决策。
- **慢查询日志**：> 5 秒的查询自动记录到慢查询日志。

### 数据源注册

```python
from columnar_client import ColumnarGatewayClient

client = ColumnarGatewayClient("http://localhost:50051")

# 注册 PostgreSQL
ok, msg = client.register_datasource(
    name="pg_db",
    datasource_type="postgres",
    connection_string="postgresql://user:pass@host:5432/dbname",
)

# 注册 MySQL
ok, msg = client.register_datasource(
    name="mysql_db",
    datasource_type="mysql",
    connection_string="mysql://user:pass@host:3306/dbname",
)

# 注册 S3 Parquet
ok, msg = client.register_datasource(
    name="s3_data",
    datasource_type="s3_parquet",
    connection_string="",
    options={
        "bucket": "my-bucket",
        "prefix": "data/",
        "region": "us-east-1",
    },
)

# 注册本地 Parquet
ok, msg = client.register_datasource(
    name="local_data",
    datasource_type="local_parquet",
    connection_string="",
    options={"path": "./parquet_files"},
)

# 列出已注册数据源
print(client.list_datasources())
```

### 跨数据源 Join

```python
# 跨 PostgreSQL 和 MySQL Join
sql = """
SELECT u.id, u.name, o.order_id, o.amount
FROM pg_db.users u
JOIN mysql_db.orders o ON u.id = o.user_id
WHERE u.country = 'CN'
  AND o.created_at >= '2024-01-01'
ORDER BY o.amount DESC
LIMIT 100
"""

result = client.federated_query(sql, include_explain=True)
print(f"Rows: {len(result.table)}")
print(f"Elapsed: {result.elapsed_ms}ms")

# 查看执行计划
if result.execution_plan:
    import json
    print(json.dumps(result.execution_plan, indent=2))
```

### 执行计划（Explain）

```python
# 仅获取执行计划，不执行查询
plan_text, plan_json = client.explain(sql, verbose=True)
print(plan_text)
```

执行计划输出示例：

```
== Query Execution Plan ==
Optimizations: column_pruning, predicate_pushdown, join_reordering
Estimated total rows: 1000

Join: Inner
  strategy: HashJoin
  reason: build side (left) rows 50000 < hash_join_threshold 100000
  on: users.id = orders.user_id
  ├── TableScan: pg_db.users
  │     projection: id, name, country
  │     pushed_filters: ["country = 'CN'"]
  └── TableScan: mysql_db.orders
        projection: order_id, user_id, amount, created_at
        pushed_filters: ["created_at >= '2024-01-01'"]
```

### 慢查询日志

```python
# 获取最近 100 条慢查询
slow_queries = client.slow_query_log(limit=100)
for q in slow_queries:
    print(f"[{q['elapsed_ms']}ms] {q['sql'][:80]}...")
```

### CLI 参数

| 参数 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `--enable-federated-query` | true | 是否启用联合查询 |
| `--query-timeout-sec` | 60 | 默认查询超时时间（秒） |
| `--slow-query-threshold-ms` | 5000 | 慢查询阈值（毫秒） |
| `--hash-join-threshold-rows` | 100000 | Hash Join 阈值（行数） |
| `--sort-merge-join-threshold-rows` | 1000000 | Sort Merge Join 阈值（行数） |
| `--enable-predicate-pushdown` | true | 启用谓词下推 |
| `--enable-column-pruning` | true | 启用列裁剪 |
| `--enable-join-reordering` | true | 启用 Join 重排序 |

启动示例：

```bash
cargo run --release -- \
  --addr [::1]:50051 \
  --data-dir ./data \
  --optimize-schema \
  --compression lz4 \
  --enable-federated-query \
  --query-timeout-sec 120 \
  --slow-query-threshold-ms 3000
```

## 测试清单

1. Rust 网关构建：`cargo build --release`
2. Python 代码生成：`python -m columnar_client._codegen`
3. Go 代码生成：`./clients/go/scripts/generate_proto.sh`
4. Java 构建：`mvn -f clients/java/pom.xml compile`
5. 端到端：启动网关 → Python 上传 → Go 查询 → Python 下载 → Java 读取

## 目录布局

```
proto/gateway.proto           # gRPC + 消息定义
src/
  main.rs                     # 入口
  storage.rs                  # 版本化的 Arrow IPC 磁盘存储
  service.rs                  # gRPC 服务实现（含 DataFusion 查询）
  schema_optimizer.rs         # 字典编码自动检测与优化
  datasource.rs               # 数据源连接器（PostgreSQL/MySQL/S3 Parquet）
  federated_optimizer.rs      # 联合查询优化器（谓词下推/列裁剪/Join策略）
  federated_engine.rs         # 联合查询执行引擎
  join_executor.rs            # Hash Join / Sort Merge Join 实现
  query_log.rs                # 查询日志与慢查询统计
  build.rs                    # tonic-build 代码生成
clients/
  python/                     # pandas / pyarrow 客户端
  java/                       # Maven + Arrow Java 客户端
  go/                         # Go + Arrow Go 客户端
```
