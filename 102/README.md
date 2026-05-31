# XDP Kubernetes NetworkPolicy Accelerator

一个基于 XDP (eXpress Data Path) 的高性能 Kubernetes NetworkPolicy 加速器，使用 eBPF 技术实现线速流量处理。

## 特性

- **高性能**: 在网卡驱动层处理数据包，达到 10Gbps 线速零丢包
- **实时规则更新**: 通过 BPF 映射表实现热更新，不中断流量
- **Kubernetes 集成**: 自动同步 NetworkPolicy 规则
- **统计监控**: 实时显示 PPS、允许/丢弃包数量等统计信息
- **日志采样**: 每 1000 个匹配规则的数据包采样记录一条日志
- **支持最多 10000 条规则**

## 系统要求

- Linux 内核 5.4+ (推荐 5.10+)
- Go 1.21+
- Clang/LLVM 10+
- bpftool (可选，用于生成完整的 vmlinux.h)
- Kubernetes 集群 (1.20+)

## 安装和构建

### 1. 克隆项目

```bash
git clone <repository-url>
cd xdp-k8s-accel
```

### 2. 安装依赖

```bash
go mod download
```

### 3. 构建

```bash
make build
```

这将生成 `xdp-accel` 可执行文件。

### 生成完整的 vmlinux.h (可选)

如果需要使用系统的完整 BTF 信息：

```bash
make vmlinux
```

## 使用方法

### 基本用法

```bash
sudo ./xdp-accel -i <interface> --stats
```

参数说明：
- `-i, --interface`: (必需) 要附加 XDP 程序的网络接口
- `--kubeconfig`: kubeconfig 文件路径 (可选，默认使用环境变量 KUBECONFIG)
- `--stats`: 每秒显示统计信息 (可选)

### 示例

在 eth0 接口上运行并显示统计：

```bash
sudo ./xdp-accel -i eth0 --stats
```

指定 kubeconfig：

```bash
sudo ./xdp-accel -i eth0 --kubeconfig ~/.kube/config --stats
```

## 工作原理

### 架构

```
┌─────────────────────────────────────────────────────────┐
│                    Kubernetes API                       │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│              Go Controller (用户空间)                    │
│  - 同步 NetworkPolicy                                    │
│  - 管理 BPF 映射表                                       │
│  - 显示统计信息                                          │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                 BPF Maps (内核空间)                      │
│  - 规则表 (最多 10000 条)                                │
│  - 统计表                                                │
│  - 事件环缓冲区                                          │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│              XDP Program (网卡驱动层)                    │
│  - 解析数据包                                            │
│  - 匹配规则                                              │
│  - 执行允许/丢弃操作                                     │
└─────────────────────────────────────────────────────────┘
```

### 数据结构

**规则键 (RuleKey)**:
- 源 IP、目标 IP
- 源端口、目标端口
- 协议 (TCP/UDP/ICMP)

**规则值 (RuleValue)**:
- 动作 (ALLOW/DENY)
- 规则 ID

### 规则匹配

程序支持灵活的规则匹配：
- 完整五元组匹配
- 通配符匹配 (IP 或端口为 0)
- 按优先级匹配

## 性能

- **处理速度**: 可达 10Gbps 线速
- **延迟**: 微秒级
- **规则容量**: 最多 10000 条规则
- **CPU 占用**: 低，因为处理在内核层完成

## 目录结构

```
.
├── bpf/
│   ├── xdp_k8s_accel.c    # XDP 程序
│   ├── xdp_k8s_accel.h    # 头文件
│   └── vmlinux.h          # 内核类型定义
├── cmd/
│   └── xdp-accel/
│       └── main.go        # 主程序
├── pkg/
│   ├── bpf/               # BPF 封装
│   └── controller/        # Kubernetes 控制器
├── scripts/
│   └── generate-vmlinux.sh
├── Makefile
├── go.mod
└── README.md
```

## 故障排除

### XDP 加载失败

确保：
1. 内核版本 >= 5.4
2. 网卡驱动支持 XDP
3. 有足够的权限 (使用 sudo)

### 规则未生效

检查：
1. NetworkPolicy 是否正确创建
2. Pod 是否有正确的标签
3. 控制器日志

### 性能问题

确保：
1. 使用支持 XDP native 模式的网卡
2. 系统有足够的 CPU 核心
3. 关闭不必要的日志采样

## 开发

### 修改 XDP 程序

编辑 `bpf/xdp_k8s_accel.c`，然后重新构建：

```bash
make clean build
```

### 添加新功能

1. 修改 XDP 程序
2. 更新 Go 封装代码
3. 重新构建测试

## 许可证

本项目使用 GPL 许可证，因为 eBPF 程序需要 GPL 兼容的许可证。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 致谢

- Cilium eBPF 库
- Kubernetes client-go
- Linux eBPF 社区
