# K8s NetworkPolicy 审计工具

一个基于 Go + eBPF（cilium/ebpf）的 K8s 网络策略审计工具。
在内核层通过 tc (traffic control) hook 捕获 ingress/egress 方向的 TCP/UDP 包，
实时记录 Pod 之间的连接，并与 K8s API Server 同步的 NetworkPolicy 定义进行对比，
输出未授权连接的审计报告。

## 架构

```
┌──────────────────────────────────────────────────────┐
│  Host Kernel (eBPF)                                  │
│  ┌──────────┐  ┌───────────┐                         │
│  │ tc/clsact│  │ tc/clsact │  ← classifier/ingress   │
│  │ ingress  │  │  egress   │    classifier/egress    │
│  └────┬─────┘  └─────┬─────┘                         │
│       │              │  perf_event_array (events)    │
│       └─────┬────────┘                               │
│             ▼                                        │
│   BPF hash map (flows) key=saddr,daddr,dport,proto   │
└───────────────┬──────────────────────────────────────┘
                │ perfbuf
                ▼
┌──────────────────────────────────────────────────────┐
│  User space (Go)                                     │
│   ├─ perf.Reader  -> FlowTracker (聚合统计)          │
│   ├─ KubeCache (informers: Pod, NetworkPolicy)       │
│   ├─ Auditor (按 NetworkPolicy 匹配，判定违规)       │
│   └─ Reporter (table / json 输出)                    │
└──────────────────────────────────────────────────────┘
```

## 功能

- **内核层抓包**：tc ingress / egress hook，仅处理 IPv4 + TCP/UDP
- **连接跟踪**：BPF hash map 按 `(saddr, daddr, dport, proto)` 聚合
- **首包事件上报**：仅在新流产生时通过 perfbuf 上报，避免风暴
- **K8s 同步**：通过 SharedInformer 实时同步 Pod IP / labels / NetworkPolicy
- **审计引擎**：
  - 若源 Pod 有 Egress Policy 但连接目标不在允许列表 → 违规
  - 若目标 Pod 有 Ingress Policy 但连接源不在允许列表 → 违规
- **报告输出**：表格（table）或 JSON
- **优雅退出**：捕获 SIGINT/SIGTERM，退出前再审计一次

## 编译

前置条件（Linux 宿主机）：

- Go 1.22+
- clang + llvm（生成 BPF 目标文件）
- linux-headers（编译 BPF 时使用 vmlinux.h 类型）
- 内核支持 BPF TC 程序（>= 5.7，推荐 >= 6.12 以便使用 TCX attach）

```bash
# 生成 BPF 目标文件 + 编译 Go 二进制
make all

# 仅编译 BPF
make bpf

# 仅编译 Go 二进制（BPF 已编译的情况下）
make build

# 清理
make clean
```

## 运行

```bash
# 运行 30 秒后自动输出报告，表格格式打印到 stdout
sudo ./k8s-netpol-audit -duration 30s -format table

# 指定接口，JSON 格式写入文件
sudo ./k8s-netpol-audit -ifaces eth0,veth0cafe -format json -out report.json

# 持续运行，每 10 秒输出一次审计
sudo ./k8s-netpol-audit -interval 10s

# 启用主动阻断模式（TC_ACT_SHOT + RST），30 秒后退出
sudo ./k8s-netpol-audit -enforce -duration 30s -format json -out report.json

# 启用阻断模式 + 白名单配置
sudo ./k8s-netpol-audit -enforce -whitelist ./whitelist.json -duration 30s
```

### 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-ifaces` | 所有 UP 状态非 loopback 接口 | 指定要附加 tc 的接口，逗号分隔 |
| `-kubeconfig` | `~/.kube/config` | kubeconfig 路径 |
| `-format` | `table` | 报告格式：`table` / `json` |
| `-out` | stdout | 报告输出文件 |
| `-interval` | `10s` | 定时输出审计周期 |
| `-duration` | 0 | 总运行时长，0 = 一直运行直到 SIGINT |
| `-enforce` | `false` | 启用主动阻断模式（TC_ACT_SHOT + RST） |
| `-whitelist` | 空 | 白名单配置文件路径（JSON） |

## 输出示例

### 表格

```
=== K8s NetworkPolicy Audit Report ===
Generated:       2026-05-24T10:12:01Z
Total observed flows:   42
Violations:      2
Impacted pods:   2

Impacted pods:
  - default/frontend-8b9c7
  - default/backend-5a43

Violations:
SrcPod          DstPod          Proto DstPort Count Bytes SrcIPState DstIPState Reason
------          ------          ----- ------- ----- ----- ---------- ---------- ------
default/fe      default/be      TCP   8080    12    7823  active     active     ingress denied by destination NetworkPolicy
default/be      <unknown>       TCP   3306    2     451   active     released   connection to/from released IP (pod may have been recycled)
```

### JSON

```json
{
  "generated_at": "2026-05-24T10:12:01Z",
  "total_flows": 42,
  "violations": [
    {
      "src_ip": "10.244.1.5",
      "dst_ip": "10.244.2.7",
      "src_pod": "default/frontend-8b9c7",
      "dst_pod": "default/backend-5a43",
      "dst_port": 8080,
      "proto": "TCP",
      "count": 12,
      "bytes": 7823,
      "first_seen": "2026-05-24T10:12:01.123Z",
      "last_seen":  "2026-05-24T10:12:40.982Z",
      "reason": "ingress denied by destination NetworkPolicy"
    }
  ],
  "impacted_pods": [
    "default/frontend-8b9c7",
    "default/backend-5a43"
  ]
}
```

## Docker

```bash
make docker
# 需要特权 / hostNetwork，例如：
docker run --rm --privileged --network host \
    -v ~/.kube/config:/kubeconfig:ro \
    -v /sys/kernel/btf:/sys/kernel/btf:ro \
    k8s-netpol-audit:latest -kubeconfig /kubeconfig -duration 30s
```

## 项目结构

```
.
├── bpf/
│   ├── flow.c           # eBPF C 程序（tc ingress/egress，含 TC_ACT_SHOT 阻断逻辑）
│   └── vmlinux.h        # 内核 BTF 类型定义（可通过 `make vmlinux` 重新生成）
├── bpf_bpfel.go         # bpf2go 风格的加载器 + //go:embed bpf_bpfel.o
├── main.go              # 程序入口、tc 附加、perf 事件循环、RST 发送
├── tracker.go           # 连接跟踪器（聚合次数 / 流量）
├── kube.go              # K8s informer 缓存（Pod、NetworkPolicy、IP 历史映射）
├── auditor.go           # 审计引擎（NetworkPolicy 匹配、白名单检查、阻断 key 收集）
├── whitelist.go         # 白名单配置解析与匹配
├── rst.go               # TCP RST 原始套接字构造与发送
├── report.go            # 表格 / JSON 报告输出
├── Makefile
├── Dockerfile
└── go.mod
```

## IP 历史映射说明

为了解决 Pod 频繁重建导致 IP 变化时的审计误报，本工具实现了 IP→Pod 的历史映射：

- **`PodIPRecord`**：每个 IP 可对应多条记录，记录不同时间段归属的 Pod（状态：`active` / `released`）
- **事件处理**：
  - Pod 创建 / IP 分配 → 新增 `active` 记录，同步写入 eBPF `ip_meta` map
  - Pod IP 变更 → 旧 IP 标记 `released`，新 IP 新建 `active` 记录
  - Pod 删除 → IP 标记 `released`，同步更新 eBPF `ip_meta` map
- **历史保留**：每条记录保留 24 小时（`PodIPHistoryTTL`），每小时 GC 一次
- **审计查询**：对每条连接，按 `FirstSeen` 时间点调用 `LookupPodHistorical(ip, ts)` 还原当时所属的 Pod
- **热更新**：通过 eBPF `Map.Put/Delete` 动态更新 `ip_meta` LRU_HASH map，完全不中断包捕获

## 主动阻断模式

当使用 `-enforce` 标志时，工具进入主动阻断模式：

- **eBPF 层**：检测到违规连接时返回 `TC_ACT_SHOT` 丢弃数据包
- **RST 双端发送**：对 TCP 违规连接，通过原始套接字向双方发送 RST 包终止连接
- **trace_pipe 输出**：通过 `bpf_printk` 输出阻断日志，可通过 `cat /sys/kernel/debug/tracing/trace_pipe` 查看
- **性能影响**：eBPF 使用 LRU_HASH map（最大 4096 条）和简单的 hash 查找，阻断逻辑仅增加常数级开销，对业务性能影响 <5%

```bash
# 启用阻断模式
sudo ./k8s-netpol-audit -enforce -duration 30s

# 启用阻断模式并指定白名单
sudo ./k8s-netpol-audit -enforce -whitelist ./whitelist.json -duration 30s
```

## 白名单配置

白名单文件为 JSON 格式，包含要豁免的连接规则：

```json
[
  {
    "src_cidr": "10.244.1.0/24",
    "dst_cidr": "10.244.2.0/24",
    "port": 3306,
    "proto": "TCP"
  },
  {
    "src_cidr": "0.0.0.0/0",
    "dst_cidr": "10.244.3.5/32",
    "port": 8080,
    "proto": "TCP"
  }
]
```

规则字段说明：
- `src_cidr` / `dst_cidr`：CIDR 格式 IP 段，匹配源/目标 IP
- `port`：目标端口，0 表示所有端口
- `proto`：协议（`TCP` / `UDP`），空或 `"*"` 表示所有协议

## 阻断日志

阻断日志可通过两种方式查看：

```bash
# 方式 1：trace_pipe（eBPF bpf_printk 输出）
sudo cat /sys/kernel/debug/tracing/trace_pipe

# 方式 2：程序 stdout（block_events perf 输出）
# 包含：源IP:端口 -> 目标IP:端口 协议 seq ack flags
```

## 性能说明

| 场景 | 开销 |
|------|------|
| 仅审计模式 | 每次数据包进入 eBPF，一次 LRU_HASH 查找（flows map），首包上报 perf |
| 阻断模式（无命中） | 额外一次 LRU_HASH 查找（blocked map），常数级开销 |
| 阻断模式（有命中） | 额外一次 LRU_HASH 查找 + TC_ACT_SHOT 返回，无 perf 上报风暴 |

阻断模式的额外开销 <5%，主要取决于 blocked map 的命中频率。

## 注意事项

- **阻断模式会实际丢弃流量**，请先在测试环境验证，建议先以仅审计模式运行确认规则正确
- 需要 CAP_NET_ADMIN / CAP_SYS_ADMIN 权限（sudo 或 privileged 容器）
- 建议在单独的 netns 或节点上运行，避免干扰其他 tc 规则
- vmlinux.h 与内核版本强相关，跨内核版本部署请重新运行 `make vmlinux`
