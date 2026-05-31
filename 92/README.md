# io-qos - 容器磁盘IO QoS控制工具

基于 **cgroups v2 io控制器** 的容器磁盘IO QoS控制CLI工具，使用Go语言开发。

## 功能特性

- ✅ **读写带宽限制** - 为容器设置读/写带宽上限（支持 KB/MB/GB/TB 单位）
- ✅ **读写IOPS限制** - 为容器设置读/写IOPS上限
- ✅ **IO优先级** - 支持 high/medium/low 三级优先级（映射为 io.weight 权重）
- ✅ **多容器同时设置** - 支持对多个容器批量应用相同限制
- ✅ **自动容器发现** - 支持通过 docker inspect 和 cgroupfs 路径自动发现
- ✅ **实时监控** - `--monitor` 模式实时显示每个容器的IO使用情况
- ✅ **动态调整** - 无需重启容器即可动态调整限制
- ✅ **YAML配置文件** - 支持批量应用配置，支持默认值继承
- ✅ **配置回滚** - 自动保存回滚点，支持一键回滚到之前状态
- ✅ **Dry-run模式** - 预览计划的操作，不实际应用

## 系统要求

- Linux 内核 >= 4.18（支持 cgroups v2）
- 已启用 cgroups v2（`grep cgroup2 /proc/mounts`）
- root 权限或 CAP_SYS_ADMIN 能力
- Go >= 1.21（编译时）

## 安装

```bash
# 克隆代码
git clone <repository-url>
cd io-qos

# 编译
go build -o io-qos ./cmd/io-qos

# 安装到系统路径
sudo mv io-qos /usr/local/bin/
```

## 快速开始

### 1. 列出可发现的容器

```bash
io-qos list
io-qos list -v  # 详细输出，包含标签信息
io-qos list --runtime docker  # 只显示docker容器
```

### 2. 设置容器IO限制

```bash
# 设置单个容器的写带宽限制为10MB/s
sudo io-qos set container1 --write-bps 10MB

# 设置多个容器的读写IOPS和优先级
sudo io-qos set container1 container2 \
  --read-iops 1000 \
  --write-iops 500 \
  --priority high

# 支持的带宽单位: KB, MB, GB, TB
sudo io-qos set container1 --read-bps 1GB --write-bps 500MB

# Dry-run模式，预览不执行
io-qos set container1 --write-bps 10MB --dry-run
```

### 3. 查看当前IO限制

```bash
io-qos show container1
io-qos show container1 container2 -v  # 详细输出
```

### 4. 实时监控IO使用情况

```bash
# 监控单个容器
sudo io-qos monitor container1

# 监控多个容器，刷新间隔2秒
sudo io-qos monitor container1 container2 --interval 2s

# 监控所有容器
sudo io-qos monitor --all

# 禁用彩色输出
io-qos monitor container1 --no-color
```

监控界面显示：
- 容器名称
- 当前读带宽 (Read BPS)
- 当前写带宽 (Write BPS)
- 当前读IOPS
- 当前写IOPS
- 等待队列长度
- 等待时间

### 5. 使用YAML配置文件批量应用

#### 生成配置模板
```bash
io-qos apply --generate config.yaml
```

#### 配置文件示例 (`config.yaml`)
```yaml
version: "1.0"

defaults:
  read_bps: 0          # 默认读带宽 (0=无限制)
  write_bps: 0         # 默认写带宽
  read_iops: 0         # 默认读IOPS
  write_iops: 0        # 默认写IOPS
  priority: medium     # 默认优先级

rules:
  - container_id: "mysql-prod-01"
    limits:
      read_bps: 104857600    # 100MB/s
      write_bps: 52428800    # 50MB/s
      read_iops: 2000
      write_iops: 1000
      priority: high

  - container_id: "redis-cache-02"
    cgroup_path: "/sys/fs/cgroup/docker/abc123def456789"  # 可选，手动指定cgroup路径
    limits:
      read_bps: 52428800     # 50MB/s
      write_bps: 10485760    # 10MB/s
      read_iops: 1000
      write_iops: 500
      priority: medium
```

#### 应用配置
```bash
sudo io-qos apply config.yaml

# 试运行
io-qos apply config.yaml --dry-run

# 详细模式，显示默认值
io-qos apply config.yaml -v
```

### 6. 回滚配置

每次使用 `apply` 或 `set` 命令时，系统会自动保存回滚点。

```bash
# 回滚到上一个状态
sudo io-qos rollback

# 列出所有可用的回滚点
io-qos rollback --list
io-qos rollback --list -v  # 详细显示每个回滚点的规则

# 预览回滚内容
io-qos rollback --dry-run
```

### 7. 重置IO限制

```bash
# 重置单个容器
sudo io-qos reset container1

# 重置多个容器
sudo io-qos reset container1 container2

# 重置所有容器
sudo io-qos reset --all

# 预览
io-qos reset container1 --dry-run
```

## 命令速查表

| 命令 | 描述 |
|------|------|
| `io-qos list` | 列出所有可发现的容器 |
| `io-qos set` | 设置容器IO限制 |
| `io-qos show` | 显示容器当前IO限制 |
| `io-qos monitor` | 实时监控容器IO使用 |
| `io-qos apply` | 批量应用YAML配置 |
| `io-qos rollback` | 回滚到上一个配置 |
| `io-qos reset` | 重置容器IO限制 |
| `io-qos help` | 显示帮助信息 |

## 全局选项

| 选项 | 描述 | 默认值 |
|------|------|--------|
| `--cgroup-root` | cgroup根目录路径 | `/sys/fs/cgroup` |
| `-v, --verbose` | 启用详细输出 | `false` |

## Monitor命令选项

| 选项 | 描述 | 默认值 |
|------|------|--------|
| `--all` | 监控所有可发现的容器 | `false` |
| `--interval` | 监控刷新间隔 | `1s` |
| `--no-color` | 禁用彩色输出 | `false` |
| `--auto-adjust` | 启用饿死检测和权重自动调整 | `false` |
| `--starvation-timeout` | 饿死检测超时时间 | `30s` |
| `--cooldown-time` | 权重恢复后的冷却时间 | `5s` |
| `--min-boost-time` | 最小权重提升持续时间 | `10s` |
| `--auto-throttle` | 启用IO延迟监控和主动限速 | `false` |
| `--latency-threshold` | IO延迟阈值(ms) | `50` |
| `--latency-duration` | 延迟持续时间 | `10s` |
| `--reduction-ratio` | 限速缩减比例 | `0.5` |
| `--min-bps` | 保底带宽(B/s) | `1048576` |
| `--min-iops` | 保底IOPS | `10` |
| `--web` | 启用Web控制面板 | `false` |
| `--web-port` | Web控制面板端口 | `8080` |
| `--log-level` | 日志级别 | `info` |
| `--log-dir` | 日志目录路径 | `/var/log/io-qos` |

## cgroups v2 io控制器说明

本工具直接操作cgroups v2的以下接口文件：

| 文件 | 用途 |
|------|------|
| `io.max` | 设置带宽和IOPS限制 |
| `io.weight` | 设置IO优先级权重 (1-100) |
| `io.stat` | 读取IO统计信息 |
| `io.pressure` | 读取IO压力信息 |

### 优先级映射

| 优先级 | io.weight 权重 |
|--------|---------------|
| high | 100 |
| medium | 50 |
| low | 10 |

### io.max 格式说明

```
<major>:<minor> rbps=<value> wbps=<value> riops=<value> wiops=<value>
```

其中：
- `rbps` - 读字节每秒 (bytes per second)
- `wbps` - 写字节每秒
- `riops` - 读IO操作每秒
- `wiops` - 写IO操作每秒
- `max` - 表示无限制

## 容器发现机制

工具按以下顺序尝试发现容器：

1. **Docker API**: 执行 `docker inspect <container_id>` 获取容器信息
2. **CgroupFS扫描**: 在 `/sys/fs/cgroup` 下搜索包含容器ID前缀的目录
3. **直接路径**: 如果传入的是 `/` 开头的路径，直接作为cgroup路径使用

支持的cgroup路径模式：
- `/sys/fs/cgroup/docker/<container-id>`
- `/sys/fs/cgroup/system.slice/docker-<container-id>.scope`
- `/sys/fs/cgroup/kubepods/besteffort/pod<pod-id>/<container-id>`

## 项目结构

```
io-qos/
├── cmd/
│   └── io-qos/
│       └── main.go          # 程序入口
├── internal/
│   ├── types/
│   │   └── types.go         # 类型定义
│   └── cli/
│       ├── root.go          # 根命令
│       ├── set.go           # set 命令
│       ├── show.go          # show 命令
│       ├── list.go          # list 命令
│       ├── monitor.go       # monitor 命令
│       ├── apply.go         # apply 命令
│       ├── rollback.go      # rollback 命令
│       └── reset.go         # reset 命令
├── pkg/
│   ├── cgroup/
│   │   └── cgroup.go        # cgroups v2 io控制器操作
│   ├── container/
│   │   └── discovery.go     # 容器发现
│   ├── monitor/
│   │   └── monitor.go       # IO监控
│   └── config/
│       └── manager.go       # 配置管理和回滚
├── examples/
│   └── config.yaml          # 配置示例
├── go.mod
├── go.sum
└── README.md
```

## 使用示例场景

### 场景1: 数据库容器IO隔离

```bash
# 给生产数据库高IO优先级和足够的带宽
sudo io-qos set mysql-prod \
  --read-bps 200MB \
  --write-bps 100MB \
  --read-iops 5000 \
  --write-iops 2000 \
  --priority high

# 给开发数据库较低的限制
sudo io-qos set mysql-dev \
  --read-bps 50MB \
  --write-bps 20MB \
  --priority low
```

### 场景2: 批量应用配置

```bash
# 生成配置模板
io-qos apply --generate io-policy.yaml

# 编辑配置文件
vim io-policy.yaml

# 应用配置
sudo io-qos apply io-policy.yaml

# 发现配置有误，立即回滚
sudo io-qos rollback
```

### 场景3: 实时监控IO瓶颈

```bash
# 监控所有容器的IO使用
sudo io-qos monitor --all --interval 1s

# 观察到某个容器IO等待队列过长，调整限制
sudo io-qos set slow-container \
  --read-bps 200MB \
  --write-bps 100MB \
  --priority high
```

## 常见问题

### Q: 为什么需要root权限？
A: 操作cgroups文件系统需要root权限或 `CAP_SYS_ADMIN` 能力。

### Q: 支持cgroups v1吗？
A: 不支持。本工具专为cgroups v2设计，利用其io控制器的完整功能。

### Q: 如何检查系统是否启用cgroups v2？
A: 执行 `grep cgroup2 /proc/mounts`，有输出则表示已启用。

### Q: 设置的限制什么时候生效？
A: 立即生效，无需重启容器。cgroups v2的限制是动态应用的。

### Q: 回滚点保存在哪里？
A: 保存在当前目录下的 `.io-qos-rollback/` 目录中，格式为JSON。

### Q: 可以限制特定块设备吗？
A: 目前版本会对所有块设备应用相同的限制。后续版本可能支持按设备单独配置。

## License

MIT License
