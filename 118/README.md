# LXC容器热迁移工具 (lxc-migrate)

基于CRIU技术的LXC容器热迁移CLI工具，支持将运行中的容器从一个物理主机迁移到另一个主机，服务中断时间<100ms。

## 功能特性

- **热迁移**: 不停机迁移运行中的LXC容器
- **CRIU集成**: 使用CRIU checkpoint/restore容器进程状态
- **TCP流式传输**: 高效传输容器状态数据
- **预拷贝模式**: 先传输大部分内存，最后停止容器传输脏页
- **后拷贝模式**: 先恢复进程，再按需拉取内存页
- **智能预测**: 基于脏页速率和带宽自动选择最优迁移模式
- **预测评估**: `--predict` 模式先评估再迁移
- **在线学习**: 迁移完成后自动反馈，模型持续优化
- **带宽限制**: `--bwlimit` 参数限制传输带宽
- **进度显示**: 实时显示迁移进度和速度
- **资源检查**: 迁移前自动检查目标主机资源（内存、CPU、磁盘）
- **网络配置**: 迁移完成后自动更新容器网络配置
- **GPU支持**: 支持NVIDIA CUDA应用的容器热迁移

## 系统要求

### 源主机和目标主机都需要:

- Linux 内核 >= 3.11 (支持CRIU)
- LXC >= 3.0
- CRIU >= 3.11
- Go >= 1.21 (编译时)

### 内核配置要求:

```
CONFIG_CHECKPOINT_RESTORE=y
CONFIG_NAMESPACES=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y
CONFIG_PID_NS=y
CONFIG_NET_NS=y
```

## 安装

### 从源码编译

```bash
git clone https://github.com/lxc-migrate/lxc-migrate.git
cd lxc-migrate
go build -o lxc-migrate .
sudo mv lxc-migrate /usr/local/bin/
```

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install lxc criu

# CentOS/RHEL
sudo yum install lxc criu
```

## 使用方法

### 1. 在目标主机启动守护进程

```bash
sudo lxc-migrate daemon --port 9999 --data-dir /tmp/lxc-migrate
```

### 2. 在源主机执行迁移

```bash
# 基本迁移
sudo lxc-migrate migrate mycontainer --host 192.168.1.100 --port 9999

# 启用预拷贝模式（推荐，减少停机时间）
sudo lxc-migrate migrate mycontainer --host 192.168.1.100 --pre-copy

# 限制带宽为10MB/s
sudo lxc-migrate migrate mycontainer --host 192.168.1.100 --bwlimit 10240

# 跳过资源检查
sudo lxc-migrate migrate mycontainer --host 192.168.1.100 --skip-check

# 强制迁移（忽略资源不足）
sudo lxc-migrate migrate mycontainer --host 192.168.1.100 --force
```

### 3. 检查容器和资源

```bash
# 检查本地容器
lxc-migrate check mycontainer

# 检查目标主机资源
lxc-migrate check --host 192.168.1.100 --port 9999

# 同时检查容器和目标主机
lxc-migrate check mycontainer --host 192.168.1.100
```

## 命令行参数

### migrate 命令

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--host, -H` | string | 127.0.0.1 | 目标主机地址 |
| `--port, -p` | int | 9999 | 目标主机端口 |
| `--checkpoint-dir` | string | /tmp/lxc-migrate | 检查点临时目录 |
| `--bwlimit` | int | 0 | 带宽限制(KB/s)，0表示无限制 |
| `--pre-copy` | bool | false | 启用预拷贝模式 |
| `--pre-copy-iter` | int | 3 | 预拷贝迭代次数 |
| `--verbose, -v` | bool | false | 详细输出 |
| `--skip-check` | bool | false | 跳过资源检查 |
| `--force` | bool | false | 强制迁移(忽略资源不足) |

### daemon 命令

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--port, -p` | int | 9999 | 监听端口 |
| `--data-dir` | string | /tmp/lxc-migrate | 数据接收目录 |
| `--verbose, -v` | bool | false | 详细输出 |

### check 命令

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--host, -H` | string | - | 目标主机地址 |
| `--port, -p` | int | 9999 | 目标主机端口 |

## 工作原理

### 直接迁移模式

1. 冻结容器 (lxc-freeze)
2. 使用CRIU创建容器检查点
3. 解冻容器 (可选)
4. 通过TCP传输检查点数据到目标主机
5. 目标主机接收数据并恢复容器
6. 更新容器网络配置

### 预拷贝模式

1. 第N-1次迭代:
   - 预拷贝内存页到目标主机
   - 继续运行容器
2. 最后一次迭代:
   - 冻结容器
   - 传输脏页（增量）
3. 目标主机合并所有数据并恢复容器

## 配置文件

默认配置文件路径: `/etc/lxc-migrate/config.json`

```json
{
  "daemon": {
    "port": 9999,
    "data_dir": "/tmp/lxc-migrate",
    "log_file": "/var/log/lxc-migrate.log"
  },
  "migrate": {
    "default_port": 9999,
    "checkpoint_dir": "/tmp/lxc-migrate",
    "default_bwlimit": 0,
    "pre_copy_iter": 3
  },
  "criu": {
    "log_file": "criu.log",
    "log_level": 2,
    "shell_job": true,
    "tcp_established": true,
    "file_locks": true
  }
}
```

## 注意事项

1. **需要root权限**: lxc-migrate 需要root权限运行
2. **网络配置**: 确保源主机和目标主机之间网络畅通
3. **磁盘空间**: 确保目标主机有足够的磁盘空间存储检查点数据
4. **CRIU限制**: CRIU不支持某些场景，如：
   - 使用了特定内核特性的应用
   - 某些文件系统操作
   - 特定的设备访问

## 故障排除

### 容器恢复失败

```bash
# 检查CRIU日志
cat /tmp/lxc-migrate/<container>/criu.log

# 检查LXC日志
lxc-info -n <container>
```

### 网络连接问题

```bash
# 测试端口连通性
telnet <target-host> 9999

# 检查防火墙
sudo iptables -L -n | grep 9999
```

### 资源不足

```bash
# 检查目标主机资源
lxc-migrate check --host <target-host>

# 检查容器资源使用
lxc-cgroup -n <container> memory.usage_in_bytes
```

## 项目结构

```
lxc-migrate/
├── main.go                 # 入口文件
├── cmd/                    # CLI命令
│   ├── root.go            # 根命令
│   ├── migrate.go         # 迁移命令
│   ├── daemon.go          # 守护进程命令
│   └── check.go           # 检查命令
├── pkg/
│   ├── lxc/               # LXC操作
│   │   ├── lxc.go         # LXC容器管理
│   │   └── criu.go        # CRIU集成
│   ├── gpu/               # GPU处理
│   │   └── gpu.go         # GPU检测、信号通知、状态保存/恢复
│   ├── transfer/          # 数据传输
│   │   └── transfer.go    # TCP传输
│   ├── resource/          # 资源检查
│   │   └── resource.go    # 资源管理
│   ├── network/           # 网络配置
│   │   └── network.go     # 网络管理
│   ├── progress/          # 进度显示
│   │   └── progress.go    # 进度条
│   ├── predict/           # 预测模型
│   │   ├── dirtyrate.go   # 脏页速率检测
│   │   ├── model.go       # 线性回归模型、模式选择
│   │   └── storage.go     # 历史数据、在线学习
│   └── config/            # 配置管理
│       └── config.go      # 配置文件
└── go.mod                 # Go模块定义
```

## 许可证

MIT License
