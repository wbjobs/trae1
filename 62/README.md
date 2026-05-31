# NVMe-oF Target (SPDK)

基于 **SPDK (Storage Performance Development Kit)** 的 NVMe over Fabrics (TCP) 目标端服务，
将本地 NVMe 磁盘通过 TCP 网络暴露给远端 Initiator，支持多命名空间（每个命名空间对应一个独立块设备）、
基础 NVMe 命令集（读、写、Identify、Log Pages）、热增/删除命名空间、以及 CLI 工具查看 Initiator 与 I/O 统计。

## 目录结构

```
├── CMakeLists.txt        # CMake 构建脚本，依赖 SPDK
├── config/target.json    # 配置文件样例（含复制配置）
├── src/nvmeof_target.c   # 目标端主程序
├── src/config_parser.c   # JSON 配置解析（含复制/备节点字段）
├── src/config_parser.h
├── src/nvmeof_lock.c     # 区域锁管理器（5 秒超时自动释放）
├── src/nvmeof_lock.h
├── src/bdev_lock.c       # 带锁感知的 SPDK vbdev（写屏障 FUA）
├── src/bdev_lock.h
├── src/nvmeof_repl.c     # 复制管理器（RDMA 连接、3 副本一致性、健康检查）
├── src/nvmeof_repl.h
├── src/bdev_repl.c       # 复制感知 vbdev（拦截写 I/O，异步复制）
├── src/bdev_repl.h
├── src/nvmeof_rpc.c      # JSON-RPC 热管理 + 锁管理 + 复制管理
├── src/nvmeof_rpc.h
├── src/nvmeof_cli.c      # 命令行工具（含 repl 子命令）
└── README.md
```

## 构建前置条件

- Linux x86_64（SPDK 仅支持 Linux 用户态驱动）
- SPDK >= 23.09（已编译并 `make install` 或设置 `SPDK_ROOT_DIR`）
- CMake >= 3.16
- GCC/Clang（支持 C11）
- 巨大页已配置：`vm.nr_hugepages=4096`
- 已绑定 nvme 磁盘到 vfio/uio 驱动（`scripts/setup.sh`）

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

生成：`nvmeof_target`、`nvmeof_cli`。

## 运行

1. 准备配置 `config/target.json`
2. 启动 target：`sudo ./nvmeof_target -c ../config/target.json`
3. 新开终端，使用 CLI：
   - 列出 Initiator：`./nvmeof_cli -s /var/tmp/nvmeof.sock initiator list`
   - 查看 I/O 统计：`./nvmeof_cli -s /var/tmp/nvmeof.sock io stats`
   - 添加命名空间：`./nvmeof_cli -s /var/tmp/nvmeof.sock ns add --subsys nqn.2024-01.io.spdk:nvme --bdev Nvme0n1`
   - 删除命名空间：`./nvmeof_cli -s /var/tmp/nvmeof.sock ns delete --subsys nqn.2024-01.io.spdk:nvme --nsid 1`

## Initiator 连接（Linux 主机）

```bash
modprobe nvme-tcp
nvme discover -t tcp -a <target-ip> -s 4420
nvme connect -t tcp -a <target-ip> -s 4420 -n nqn.2024-01.io.spdk:nvme
```

## 分布式区域锁与写屏障

为解决多 Initiator 并发写入同一命名空间不同区域导致的数据不一致（读到旧数据）问题，
实现了以下机制：

### 区域锁（Region Lock）
- 每个命名空间维护独立的锁表（`src/nvmeof_lock.h/c`）
- 写入前必须获取对应 LBA 范围的区域锁
- 锁冲突返回 `-EBUSY`，写操作失败（不会读到旧数据）
- **5 秒超时自动释放**（`NVME_LOCK_TIMEOUT_SEC`），防止 Initiator 崩溃导致死锁
- 同一 owner（I/O channel）对重叠区域的写操作自动放行

### 写屏障（Write Barrier / FUA）
- 配置 `enable_barrier: true`（JSON 配置或 CLI `--barrier`）
- 每次写操作完成后自动触发 `FLUSH`（写回缓存到持久介质）
- 保证写顺序：后续读操作一定能看到之前已完成的写
- SPDK vbdev 层实现（`src/bdev_lock.h/c`），对 Initiator 透明

### 锁管理 RPC
| RPC 方法 | 功能 |
|---|---|
| `nvmeof_lock_list` | 列出所有活动锁（nsid, owner, lba, flags, age） |
| `nvmeof_lock_release_all` | 释放指定 owner 的所有锁 |
| `nvmeof_lock_cleanup` | 立即清理所有超时锁 |

### 配置示例
```json
{
  "namespaces": [{
    "bdev_name": "Nvme0n1",
    "nsid": 1,
    "enable_barrier": true
  }]
}
```

## 异步复制与故障切换

支持 **1 主 + 2 备** 三副本同步复制，主节点写入后通过 **RDMA** 同步到两个备节点，
所有节点写入成功才返回完成（3 副本一致性）。主节点故障时 Initiator 侧通过 **MPIO 多路径**
自动切换到备节点，切换时间 < 3 秒。

### 复制架构
- **主节点 (Primary)**：接收 Initiator 写入，本地写成功后异步通过 SPDK NVMe-oF RDMA
  客户端写往两个备节点（`src/bdev_repl.c` 拦截写 I/O → `src/nvmeof_repl.c` 发起 RDMA 写）
- **备节点 (Backup)**：独立运行 `nvmeof_target`（同配置但 `role=backup`），只接收复制流量
- **仲裁策略**：当前实现为"3 副本都成功才返回成功"（严格一致性），可按需调整为 2/3 quorum

### 故障切换
- 主节点每 500ms 对所有备节点进行健康检查（`nvmeof_repl_health_check`）
- 连续 3 次失败标记节点为 `DEGRADED`
- **Initiator 侧**：使用 Linux `dm-multipath`，主路径失败自动切换到备路径，切换时间 < 3s

  ```bash
  # /etc/multipath.conf 示例
  devices {
      device {
          vendor "NVME"
          product "SPDK"
          path_selector "round-robin 0"
          path_checker tur
          failback immediate
          rr_min_io 100
      }
  }
  multipath -a /dev/nvme0n1
  ```

### 配置示例（三副本）
```json
{
  "subsystems": [{
    "nqn": "nqn.2024-01.io.spdk:nvme",
    "replication": {
      "role": "primary",
      "backups": [
        {"traddr": "192.168.10.11", "trsvcid": "4420",
         "nqn": "nqn.2024-01.io.spdk:nvme-backup1", "remote_nsid": 1},
        {"traddr": "192.168.10.12", "trsvcid": "4420",
         "nqn": "nqn.2024-01.io.spdk:nvme-backup2", "remote_nsid": 1}
      ]
    },
    "namespaces": [
      {"bdev_name": "Nvme0n1", "nsid": 1,
       "enable_barrier": true, "enable_replication": true}
    ]
  }]
}
```

### 管理接口
| 命令 | 功能 |
|---|---|
| `nvmeof_cli repl status` | 查看复制状态、节点健康、统计 |
| `nvmeof_cli repl health_check` | 立即触发一次健康检查 |
| `nvmeof_cli repl backup_add --nsid 1 --traddr <ip> --trsvcid <port> --nqn <nqn>` | 热添加备节点 |

## 性能目标

单客户端 4K 随机读 IOPS >= 50 万，需要：
- 使用 SPDK TCP 传输（用户态轮询，无上下文切换）
- 绑核（`-m 0x3`），至少 2 核
- 使用多队列（`--io_queues 8`）
- 目标端使用 NVMe SSD（建议 PCIe 4.0，>= 1M IOPS）
- 使用 fio 压测：`fio --name=randread --rw=randread --bs=4k --numjobs=8 --iodepth=64 --direct=1 --ioengine=io_uring --filename=/dev/nvmeXn1`
