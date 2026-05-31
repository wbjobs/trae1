# ICS Protocol Fuzzer

工控协议模糊测试CLI工具，支持Modbus TCP和DNP3协议。

## 功能特性

- 支持 **Modbus TCP** (端口502) 和 **DNP3** (端口20000) 两种工控协议
- 从PCAP文件读取报文作为初始种子，或使用内置生成的样本报文
- 支持4种变异策略：
  - **位翻转** (Bit Flip) - 随机翻转数据位
  - **边界值** (Boundary Value) - 替换为边界值（0x00, 0xFF, 0x7F等）
  - **随机字节** (Random Byte) - 替换为随机字节
  - **长度溢出** (Length Overflow) - 插入或追加随机数据
- **遗传算法** - 根据前一轮测试结果动态调整变异策略权重
- 多线程并发测试（默认4线程）
- 异常检测：
  - 响应超时
  - 协议异常码
  - 设备复位/重启
  - 连接重置/拒绝
  - 服务崩溃
- 异常报文自动保存到 `./crashes/` 目录
- **--replay** 重放功能，用于复现触发的异常
- 实时状态监控和统计

## 安装

```bash
# 安装依赖
pip install -r requirements.txt

# 安装libpcap（Linux/Mac）
# Ubuntu/Debian
sudo apt-get install libpcap-dev

# CentOS/RHEL
sudo yum install libpcap-devel

# macOS (Homebrew)
brew install libpcap

# Windows
# 安装Npcap: https://nmap.org/npcap/
```

## 使用方法

### 1. 模糊测试

```bash
# 使用PCAP文件作为种子进行Modbus TCP模糊测试
python cli.py fuzz --protocol modbus --target 192.168.1.100 --pcap capture.pcap

# 使用生成的样本报文进行DNP3模糊测试，8线程
python cli.py fuzz --protocol dnp3 --target 192.168.1.101 --threads 8 --iterations 100

# 自定义超时和迭代次数
python cli.py fuzz --protocol modbus --target 192.168.1.100 --timeout 5.0 --iterations 5000

# 禁用遗传算法
python cli.py fuzz --protocol modbus --target 192.168.1.100 --no-genetic
```

### 2. 重放异常报文

```bash
# 列出所有可用的崩溃文件
python cli.py replay --protocol modbus --target 192.168.1.100 --replay list

# 重放指定的崩溃文件（按索引）
python cli.py replay --protocol modbus --target 192.168.1.100 --replay 1

# 重放指定的崩溃文件（按路径）
python cli.py replay --protocol modbus --target 192.168.1.100 --replay crashes/modbus_20260525_123456_Illegal_Function.bin

# 重放多次（默认3次）
python cli.py replay --protocol modbus --target 192.168.1.100 --replay 1 --count 5
```

### 3. 列出崩溃文件

```bash
python cli.py list
```

### 4. 查看支持的协议

```bash
python cli.py protocols
```

## 命令行参数

### fuzz 命令

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--protocol` | 协议类型: modbus/dnp3 | 必填 |
| `--target` | 目标IP或主机名 | 必填 |
| `--port` | 目标端口 | 502(Modbus)/20000(DNP3) |
| `--pcap` | PCAP种子文件路径 | 可选 |
| `--threads` | 线程数 | 4 |
| `--iterations` | 迭代次数 | 1000 |
| `--timeout` | 响应超时（秒） | 3.0 |
| `--no-genetic` | 禁用遗传算法 | False |
| `--crash-dir` | 崩溃文件保存目录 | ./crashes |

### replay 命令

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--protocol` | 协议类型: modbus/dnp3 | 必填 |
| `--target` | 目标IP或主机名 | 必填 |
| `--port` | 目标端口 | 可选 |
| `--replay` | 崩溃文件路径、"list"或索引 | 必填 |
| `--count` | 重放次数 | 3 |
| `--timeout` | 响应超时（秒） | 3.0 |

## 项目结构

```
.
├── cli.py                 # 命令行接口主程序
├── fuzzer.py              # 模糊测试核心引擎
├── protocols/
│   ├── __init__.py
│   ├── modbus_tcp.py      # Modbus TCP协议解析
│   └── dnp3.py            # DNP3协议解析
├── utils/
│   ├── __init__.py
│   ├── mutations.py       # 报文变异模块
│   ├── genetic.py         # 遗传算法策略
│   └── pcap_utils.py      # PCAP文件处理工具
├── crashes/               # 崩溃报文保存目录
├── requirements.txt       # Python依赖
└── README.md              # 本文件
```

## 工作原理

### 遗传算法变异策略

1. **初始化种群**：从种子报文变异生成初始种群
2. **适应度评估**：根据是否触发崩溃、异常响应、响应时间等计算适应度
3. **选择**：轮盘赌选择适应度高的个体
4. **交叉**：随机单点交叉组合两个父代报文
5. **变异**：按概率对报文进行变异
6. **进化**：每代更新变异策略权重，增加成功触发异常的变异类型权重

### 异常检测机制

- **Modbus TCP**: 检测功能码是否大于0x80（异常响应），解析异常码
- **DNP3**: 解析内部指示位（Internal Indications），检测设备故障、重启等状态
- **通用**: 检测连接超时、重置、拒绝等网络异常

## 安全注意事项

⚠️ **警告：仅在授权的测试环境中使用**

- 本工具用于工控设备的安全测试
- 确保已获得设备所有者的书面授权
- 不要在生产环境中使用
- 模糊测试可能导致设备不稳定或重启
- 建议在隔离的测试网络中使用

## 输出示例

```
[*] Starting fuzzing against 192.168.1.100:502
[*] Protocol: MODBUS
[*] Threads: 4
[*] Genetic algorithm: Enabled
[*] Seed packets: 48
[*] Max iterations: 1000

[*] Probing target 192.168.1.100:502...
[*] Sending probe packet (12 bytes)...
[+] Target responded with 9 bytes
[*] Initializing genetic algorithm population...
[*] Starting worker threads...

[*] Iteration 1/1000
[*] Status: 20 packets, 0 crashes, 2 exceptions, 1 timeouts, 4.02 pps
[*] Genetic: Gen 0, Best fitness: 55.00, Avg fitness: 12.35
[!] Exception: Illegal Function - bit_flip
...
```

## License

仅供安全研究和教学使用。
