# Distributed Task Orchestrator

分布式任务编排引擎，基于 Java + ZooKeeper，支持 DAG 定义任务依赖关系。

## 架构设计

### 核心组件

| 组件 | 说明 |
|------|------|
| **Master** | 通过 ZooKeeper 临时顺序节点实现选举，负责任务分片调度、Worker 崩溃检测与重调度 |
| **Worker** | 从 ZooKeeper 拉取可执行任务，创建临时节点占位，执行后上报结果到持久节点 |
| **ZooKeeper** | 存储 DAG 定义、任务运行时状态、运行中任务的临时节点 |

### ZooKeeper 节点结构

```
/orchestrator
├── /dags
│   └── /{dagId}                    # 持久节点，存储 DAG 定义 JSON
│       └── /tasks
│           └── /{taskId}           # 持久节点，存储 TaskRuntime 状态
├── /masters
│   └── /master-{seq}               # 临时顺序节点，用于 Master 选举
├── /workers
│   └── /{workerId}                 # 临时节点，Worker 注册信息
├── /running-tasks
│   └── /{dagId}_{taskId}           # 临时节点，Worker 执行时创建，崩溃后自动删除
└── /results
    └── /{dagId}
        └── /{taskId}               # 持久节点，存储任务输出结果（最大1MB）
```

### 崩溃恢复机制

1. **临时节点占位**：Worker 获取任务后在 `/running-tasks/{dagId}_{taskId}` 创建 EPHEMERAL 临时节点，数据为 workerId
2. **崩溃自动释放**：Worker 进程被 kill 后，ZooKeeper 会话超时，临时节点自动删除
3. **Master 检测**：Master 每 5 秒扫描所有 RUNNING 状态的任务，检查临时节点是否存在
4. **重调度**：若临时节点不存在，Master 将任务状态改为 RETRYING，增加 retryCount，未超过 maxRetries 则其他 Worker 可重新获取执行
5. **无限重试防护**：全局最大重试次数限制为 3 次（`MAX_GLOBAL_RETRIES`），超过后标记为 FAILED

### 任务状态流转

```
PENDING → RUNNING → SUCCESS
              ↘ RETRYING → RUNNING → ... (最多重试3次)
              ↘ TIMEOUT → RETRYING → ...
              ↘ FAILED (超过最大重试)
```

## 任务类型

### 1. SHELL - Shell 命令执行

```json
{
  "type": "SHELL",
  "params": {
    "command": "echo hello && python3 script.py",
    "workingDir": "/opt/workspace"
  }
}
```

### 2. HTTP - HTTP 请求调用

```json
{
  "type": "HTTP",
  "params": {
    "url": "https://api.example.com/endpoint",
    "method": "POST",
    "headers": {
      "Content-Type": "application/json"
    },
    "body": "{\"key\": \"value\"}"
  }
}
```

### 3. SQL - SQL 语句执行

```json
{
  "type": "SQL",
  "params": {
    "jdbcUrl": "jdbc:mysql://localhost:3306/db",
    "username": "user",
    "password": "pass",
    "driver": "com.mysql.cj.jdbc.Driver",
    "sql": "SELECT * FROM table LIMIT 10"
  }
}
```

## DAG 定义

```json
{
  "id": "my_pipeline",
  "name": "Data Pipeline",
  "tasks": {
    "task_a": { "id": "task_a", "name": "Extract", "type": "HTTP", "...": "..." },
    "task_b": { "id": "task_b", "name": "Transform", "type": "SHELL", "...": "..." },
    "task_c": { "id": "task_c", "name": "Load", "type": "SQL", "...": "..." }
  },
  "dependencies": {
    "task_a": [],
    "task_b": ["task_a"],
    "task_c": ["task_b"]
  }
}
```

- `dependencies` 中 key 为任务 ID，value 为该任务依赖的前置任务 ID 列表
- 只有前置任务全部 SUCCESS 后，后续任务才会被调度

## 任务间数据传递

任务间支持数据传递：上游任务的输出（stdout）会被存储到 ZooKeeper，下游任务可以通过占位符引用上游结果。

### 工作原理

1. **结果存储**：上游任务执行成功后，其 stdout 输出（JSON 格式）被存储到 ZooKeeper 的 `/orchestrator/results/{dagId}/{taskId}` 持久节点
2. **大小限制**：每个结果最大 1MB，超出部分自动截断
3. **动态替换**：下游任务的 `params` 中可使用 `{{task_id.output.field}}` 占位符，执行前会被自动替换为上游任务的实际输出值

### 占位符语法

```
{{task_id}}                    # 引用整个上游任务的原始输出字符串
{{task_id.output}}             # 同上
{{task_id.output.field}}       # 引用上游任务 JSON 输出中的字段
{{task_id.field}}              # 同上（output 可省略）
{{task_id.field.nested}}       # 支持多级嵌套引用
```

### 数据传递示例

```json
{
  "id": "data_passing_demo",
  "name": "Data Passing Demo",
  "tasks": {
    "fetch_user": {
      "type": "HTTP",
      "params": {
        "url": "https://api.example.com/users/12345",
        "method": "GET"
      }
    },
    "process_user": {
      "type": "SHELL",
      "params": {
        "command": "echo '{\"userId\": \"{{fetch_user.id}}\", \"username\": \"{{fetch_user.username}}\"}'"
      }
    },
    "save_user": {
      "type": "SQL",
      "params": {
        "jdbcUrl": "jdbc:mysql://localhost:3306/app_db",
        "sql": "INSERT INTO users (id, name) VALUES ('{{process_user.userId}}', '{{process_user.username}}')"
      }
    }
  },
  "dependencies": {
    "fetch_user": [],
    "process_user": ["fetch_user"],
    "save_user": ["process_user"]
  }
}
```

### 上游结果就绪检查

Master 在调度任务时，不仅检查 DAG 依赖是否完成，还会检查被引用的上游任务结果是否已存储到 ZooKeeper。如果上游结果尚未就绪，任务会被延迟调度。

### CLI 查看结果

```bash
# 查看指定任务的输出结果
java -jar task-orchestrator-jar-with-dependencies.jar result -d data_passing_demo -t fetch_user

## 任务配置参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `timeoutMs` | long | 300000 (5分钟) | 任务执行超时时间 |
| `maxRetries` | int | 3 | 最大重试次数（全局上限为 3） |

## 使用方式

### 1. 构建

```bash
mvn clean package
```

产出 `target/task-orchestrator-jar-with-dependencies.jar`

### 2. 启动 Master

```bash
java -cp task-orchestrator-jar-with-dependencies.jar com.orchestrator.MasterMain 127.0.0.1:2181
```

可启动多个 Master 实例，自动选举一个 Leader。

### 3. 启动 Worker

```bash
java -cp task-orchestrator-jar-with-dependencies.jar com.orchestrator.WorkerMain 127.0.0.1:2181
```

可启动多个 Worker 实例，任务自动负载均衡。

### 4. CLI 工具

```bash
# 提交 DAG
java -jar task-orchestrator-jar-with-dependencies.jar submit -f examples/dag-example.json

# 查询 DAG 任务状态
java -jar task-orchestrator-jar-with-dependencies.jar status -d example_dag_001

# 查询单个任务状态
java -jar task-orchestrator-jar-with-dependencies.jar status -d example_dag_001 -t task_extract

# 列出所有 DAG
java -jar task-orchestrator-jar-with-dependencies.jar list

# 查看 DAG 定义
java -jar task-orchestrator-jar-with-dependencies.jar dag -d example_dag_001

# 指定 ZooKeeper 地址
java -jar task-orchestrator-jar-with-dependencies.jar -z 192.168.1.100:2181 list
```

## 依赖

- Java 8+
- Apache ZooKeeper 3.8+
- Maven 3.6+

## 故障恢复流程详解

```
正常流程:
  Worker获取任务 → 创建临时节点/running-tasks/{dagId}_{taskId}
  → 执行任务 → 上报结果 → 删除临时节点

Worker崩溃流程:
  Worker进程kill → ZooKeeper会话超时 → 临时节点自动删除
  → Master检测到临时节点不存在 → 任务状态改为RETRYING
  → 增加retryCount → 若未超过maxRetries → 其他Worker重新获取执行
  → 若已超过maxRetries → 任务标记为FAILED
```
