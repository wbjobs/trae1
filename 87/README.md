# SCTP Multi-Path File Transfer

基于SCTP协议的多路径文件传输工具，利用SCTP的多流(Multi-Stream)和多宿主(Multi-Homing)特性，同时通过WiFi和以太网等多条路径传输大文件。

## 功能特性

- **多路径传输**: 同时利用WiFi、以太网等多条网络路径
- **多流传输**: 使用SCTP多流特性减少队头阻塞
- **多宿主支持**: 单SCTP关联支持多个IP地址
- **动态负载均衡**: 根据实时带宽自动分配数据块大小
- **路径故障自动切换**: 路径断开时自动切换到健康路径
- **端到端CRC32C校验**: 传输完成后验证文件完整性
- **实时速度显示**: 每条路径独立速度显示 + 总速度
- **断点续传**: 支持中断后继续传输
- **反向传输**: 接收端也可以向发送端发送文件

## 编译要求

- Linux内核支持SCTP (CONFIG_IP_SCTP)
- lksctp-tools 开发库
- GCC 4.8+ 或 Clang
- pthread库

## 安装依赖

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y lksctp-tools libsctp-dev gcc make
```

### CentOS/RHEL
```bash
sudo yum install -y lksctp-tools-devel gcc make
```

### Fedora
```bash
sudo dnf install -y lksctp-tools-devel gcc make
```

## 编译

```bash
cd sctp_transfer
make
```

清理编译：
```bash
make clean
```

## 使用方法

### 接收端（服务器模式）

```bash
./sctp_transfer recv [选项] <输出目录>

选项:
  -p, --port PORT     监听端口 (默认: 9000)
  -b, --bind ADDR     绑定地址 (默认: 0.0.0.0)
  -r, --reverse       允许反向传输
  -R, --resume        启用断点续传
```

示例：
```bash
./sctp_transfer recv -p 9000 -b 0.0.0.0 -R -r ./downloads
```

### 发送端（客户端模式）

```bash
./sctp_transfer send [选项] <文件> <远程IP>

选项:
  -p, --port PORT     远程端口 (默认: 9000)
  -l, --local IPS     本地绑定地址（逗号分隔）
  -r, --reverse       允许接收反向传输
  -R, --resume        启用断点续传
```

示例（双路径）：
```bash
./sctp_transfer send -p 9000 -R -r \
  -l 192.168.1.100,10.0.0.50 \
  bigfile.iso 192.168.1.200
```

## 典型应用场景

### 场景1：WiFi + 以太网双路径传输

**接收端 (192.168.1.200)**:
```bash
./sctp_transfer recv -b 0.0.0.0 -p 9000 -R ./downloads
```

**发送端 (双网卡)**:
```bash
# WiFi: 192.168.1.100, 以太网: 10.0.0.50
./sctp_transfer send -p 9000 -R \
  -l 192.168.1.100,10.0.0.50 \
  large_file.zip 192.168.1.200
```

### 场景2：断点续传

如果传输中断，重新运行相同命令即可继续：

```bash
# 首次传输中断...
./sctp_transfer send -R -l 192.168.1.100 video.mkv 192.168.1.200

# 重新运行继续传输
./sctp_transfer send -R -l 192.168.1.100 video.mkv 192.168.1.200
```

### 场景3：反向传输

传输完成后，接收端可以向发送端发送文件：

**接收端**:
```bash
./sctp_transfer recv -r -b 0.0.0.0 ./downloads
# 当主传输完成后，输入要反向发送的文件名
```

**发送端**:
```bash
./sctp_transfer send -r -l 192.168.1.100 data.bin 192.168.1.200
```

## 协议规范

### 消息格式

```
+---------------------------------------------------+
|  Magic (4B) | Ver (1B) | Type (1B) | Flags (2B) |
+---------------------------------------------------+
|         Length (4B)       |    Seq Num (4B)      |
+---------------------------------------------------+
|                    CRC32C (4B)                    |
+---------------------------------------------------+
|                    Payload (N)                    |
+---------------------------------------------------+
```

### 消息类型

| 类型 | 说明 |
|------|------|
| 1 | 握手请求 |
| 2 | 握手应答 |
| 3 | 文件元数据 |
| 4 | 文件元数据应答 |
| 5 | 数据块 |
| 6 | 数据块应答 |
| 7 | 续传请求 |
| 8 | 续传响应 |
| 9 | 传输完成 |
| 10 | 传输完成应答 |
| 11 | 心跳 |
| 12 | 错误 |

## 性能调优

### 内核参数

```bash
# 增加SCTP缓冲区
sysctl -w net.sctp.rwnd_scale=3
sysctl -w net.sctp.sndbuf_policy=0
sysctl -w net.sctp.rcvbuf_policy=0

# 启用路径MTU发现
sysctl -w net.ipv4.sctp_pmtu_disc=1
```

### 多宿主配置

确保两个端点的所有IP地址都可以互相访问。SCTP会自动在多个IP之间进行故障切换。

## 注意事项

1. **SCTP支持**: 确保Linux内核已启用SCTP模块
   ```bash
   modprobe sctp
   ```

2. **防火墙**: 允许SCTP协议通过防火墙
   ```bash
   iptables -A INPUT -p sctp --dport 9000 -j ACCEPT
   ```

3. **大文件**: 对于超过10GB的文件，建议使用断点续传(-R)

4. **路径选择**: 确保多条路径是真正独立的物理链路

## 项目结构

```
sctp_transfer/
├── Makefile
├── test_latency.sh      # 延迟差异测试脚本
├── include/
│   └── sctp_transfer.h      # 公共头文件
└── src/
    ├── main.c               # 主入口和CLI解析
    ├── crc32c.c             # CRC32C校验
    ├── sctp_socket.c        # SCTP套接字管理 + RTT测量
    ├── file_chunk.c         # 文件分块与重组
    ├── msg_protocol.c       # 消息协议处理 + NACK
    ├── reorder_buffer.c    # 重排序缓冲区(500块)
    ├── load_balance.c       # 负载均衡 + RTT智能调度
    ├── speed_display.c      # 速度显示 + RTT显示
    ├── resume.c             # 断点续传
    ├── sender.c             # 发送端逻辑 + NACK重传
    └── receiver.c           # 接收端逻辑 + NACK发送
```

## License

MIT License
