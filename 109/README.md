# DDoS 流量自动牵引系统

基于 Python、ExaBGP 和 sFlow 的 DDoS 流量自动检测和牵引系统。支持分级策略和基于机器学习的攻击类型自动识别。

## 功能特性

- **流量监控**: 支持 sFlow 协议进行实时流量采集，支持流量速率统计
- **机器学习攻击识别**:
  - 随机森林模型实时分析 sFlow 采样流量特征
  - 识别攻击类型：SYN Flood、UDP Amplification、HTTP Flood、DNS Query Flood、NTP 反射
  - 目标准确率：95%，识别延迟 < 3 秒
  - 每周自动/手动重新训练模型
- **异常检测**:
  - 流量突增检测（超过基线 3 倍）
  - SYN Flood 攻击检测
- **分级牵引策略**:
  - 流量 < 50 Gbps: 牵引到清洗中心
  - 流量 50-100 Gbps: 牵引 + 限速到 50 Gbps
  - 流量 > 100 Gbps: 直接丢弃 UDP 流量但保留 TCP
- **智能策略选择**:
  - HTTP Flood → 重定向到 WAF
  - UDP Amplification/NTP 反射 → 直接丢弃（无需清洗中心）
  - DNS Query Flood → 限速
  - SYN Flood → 重定向到清洗中心
- **自动升级**: 攻击持续时自动升级牵引级别
- **手动升级**: 支持命令行和 API 手动升级
- **自动牵引**: 通过 BGP FlowSpec 协议自动下发规则
- **REST API**: 提供查询和手动控制接口
- **自动过期**: 攻击停止 30 秒后自动撤销规则
- **历史记录**: 所有牵引事件存储到 InfluxDB
- **Dry-Run 模式**: 模拟运行不影响生产环境

## 系统架构

```
┌─────────────┐
│   sFlow     │
│  Collector  │
└──────┬──────┘
       │ sFlow 数据
       ▼
┌───────────────────────────────┐
│   Traffic Monitor         │
│   (流量监控+速率计算)     │
└──────┬────────────────────┘
       │
       ▼
┌───────────────────────────────┐
│   Feature Extractor       │
│   (特征提取模块)           │
└──────┬────────────────────┘
       │
       ▼
┌───────────────────────────────┐
│   ML Attack Classifier    │
│   (随机森林模型)           │
└──────┬────────────────────┘
       │
       ▼
┌───────────────────────────────┐
│   Anomaly Detector       │
│   (异常检测+级别判断)     │
└──────┬────────────────────┘
       │
       ▼
┌───────────────────────────────┐
│   Rule Manager           │
│   (规则管理+分级策略)   │
└──────┬────────────────────┘
       │
       ├─────────────┬─────────────┐
       ▼             ▼             ▼
┌─────────────┐ ┌───────┐  ┌──────────────┐
│ FlowSpec    │ │ API   │  │  InfluxDB    │
│ Controller  │ │Server │  │  Writer      │
└──────┬──────┘ └───────┘  └──────────────┘
       │
       ▼
┌─────────────┐
│   ExaBGP    │
└──────┬──────┘
       │ BGP FlowSpec
       ▼
┌─────────────┐
│   Router     │
└─────────────┘
```

## 安装

### 依赖安装

```bash
pip install -r requirements.txt
```

### 系统要求

- Python 3.8+
- ExaBGP 4.0+
- InfluxDB 2.0+ (可选，用于历史记录)
- scikit-learn 1.3+
- numpy 1.24+

## 配置

### 1. 系统配置 (config.yaml)

```yaml
ddos_mitigator:
  name: "DDoS Traffic Diversion System"
  dry_run: false

sflow:
  listen_port: 6343
  listen_address: "0.0.0.0"
  sampling_rate: 1000

threshold:
  traffic_multiplier: 3.0
  baseline_window: 300
  detection_window: 60
  rule_expire_seconds: 30

escalation:
  enabled: true
  check_interval: 10
  upgrade_threshold: 3

ml:
  enabled: true
  model_path: "attack_classifier.pkl"
  confidence_threshold: 0.7
  retrain_interval_days: 7
  target_accuracy: 0.95
  inference_timeout_ms: 3000
  feature_window_seconds: 60

scrubbing_center:
  ip: "10.0.0.1"
  redirect_next_hop: "10.0.0.2"
  waf_ip: "10.0.0.3"

tiered_policy:
  tiers:
    - level: 1
      name: "low"
      max_rate: 50
      action: "redirect"
      description: "流量 < 50 Gbps, 牵引到清洗中心"
    - level: 2
      name: "medium"
      max_rate: 100
      action: "rate-limit"
      rate_limit: 50
      description: "50-100 Gbps, 牵引 + 限速到 50 Gbps"
    - level: 3
      name: "high"
      max_rate: 1000
      action: "selective-drop"
      description: "> 100 Gbps, 丢弃 UDP 保留 TCP"

exabgp:
  socket_path: "/var/run/exabgp/exabgp.sock"
  local_as: 65001
  local_ip: "192.168.1.1"
  router_ip: "192.168.1.2"
  router_as: 65000

influxdb:
  url: "http://localhost:8086"
  token: "your-influxdb-token"
  org: "ddos_mitigation"
  bucket: "ddos_metrics"

api:
  host: "0.0.0.0"
  port: 8000
```

### 2. ExaBGP 配置 (exabgp.conf)

参考 `exabgp.conf` 文件进行配置。

## 使用

### 正常运行模式

```bash
python3 main.py --config config.yaml
```

### Dry-Run 模式（测试）

```bash
python3 main.py --config config.yaml --dry-run
```

### 手动升级规则

为特定的目标 IP 手动升级防护级别：

```bash
python3 main.py --config config.yaml --escalate 192.0.2.1
```

### 手动分类测试

使用 JSON 文件中的流量样本测试 ML 分类器：

```bash
python3 main.py --config config.yaml --classify samples.json
```

样本文件格式：

```json
[
  {
    "id": "sample_001",
    "features": {
      "num_packets": 5000,
      "packet_size_mean": 64,
      "protocol_udp_ratio": 0.95,
      "src_ip_entropy": 8.5,
      ...
    }
  }
]
```

### 重新训练模型

使用累积的数据重新训练模型：

```bash
python3 main.py --config config.yaml --retrain
```

### 启动 ExaBGP

```bash
exabgp exabgp.conf
```

## REST API 接口

### 健康检查

```
GET /api/v1/health
```

### 获取活跃规则

```
GET /api/v1/rules
```

### 获取统计信息

```
GET /api/v1/stats
```

### 获取策略配置

```
GET /api/v1/policy
```

### 创建手动规则

```
POST /api/v1/rules/manual
Content-Type: application/json

{
  "src_ip": "192.0.2.1",
  "dst_ip": "203.0.113.1",
  "protocol": 6,
  "dst_port": 80
}
```

### 撤销规则

```
POST /api/v1/rules/revoke
Content-Type: application/json

{
  "rule_id": "uuid-of-the-rule"
}
```

### 手动升级规则

```
POST /api/v1/rules/escalate
Content-Type: application/json

{
  "dst_ip": "203.0.113.1"
}
```

## 机器学习模块

### 特征提取

系统从 sFlow 流量中提取以下特征：

- **包大小特征**: mean, std, min, max
- **协议分布**: TCP/UDP/ICMP 比率
- **流特征**: 持续时间、每流包数、每流字节数
- **熵值**: 源IP熵、目的IP熵、源端口熵、目的端口熵
- **TCP标志**: SYN/ACK/FIN/RST 比率
- **应用层协议特征**: DNS/HTTP/HTTPS/NTP 端口比率
- **流量速率**: 包速率、字节速率

### 攻击类型识别

| 攻击类型 | 特征 | 推荐策略 |
|---------|------|---------|
| SYN Flood | 高SYN比率、低ACK比率 | 牵引到清洗中心 |
| UDP Amplification | 高UDP比率、高包大小、高端口集中 | 直接丢弃 |
| HTTP Flood | 高TCP比率、可变端口、包大小均匀 | 牵引到WAF |
| DNS Query Flood | 高UDP/53端口比率 | 限速DNS查询 |
| NTP Reflection | 高UDP/123端口比率 | 直接丢弃 |

### 模型性能

- **准确率目标**: ≥ 95%
- **识别延迟**: < 3 秒
- **模型**: 随机森林 (100 棵树，最大深度 15)
- **重新训练**: 每周自动或手动触发

## 目录结构

```
.
├── main.py              # 主程序入口
├── system_config.py     # 配置管理
├── traffic_monitor.py   # sFlow 流量监控
├── feature_extractor.py # 流量特征提取
├── ml_classifier.py    # ML攻击分类器
├── anomaly_detector.py  # 异常检测
├── flowspec_controller.py # FlowSpec 规则控制
├── rule_manager.py     # 规则管理
├── api_server.py      # REST API 服务
├── influxdb_writer.py   # InfluxDB 写入
├── config.yaml        # 配置文件示例
├── exabgp.conf        # ExaBGP 配置示例
├── requirements.txt   # 依赖包
└── README.md        # 本文档
```

## 检测逻辑

### 流量突增检测

1. 持续监控各目标 IP 的流量速率
2. 计算基线流量（过去 5 分钟平均值）
3. 当前流量超过基线的 3 倍时触发告警

### SYN Flood 检测

1. 监控 TCP SYN 数据包（无 ACK 标记）
2. 10 秒内同一目标 IP 收到超过 100 个 SYN 包时触发告警

### 分级策略

- **级别 1 (低)**: 流量 < 50 Gbps，将流量重定向到清洗中心
- **级别 2 (中)**: 50-100 Gbps，重定向到清洗中心 + 限速到 50 Gbps
- **级别 3 (高)**: > 100 Gbps，选择性丢弃 UDP 流量，保留 TCP

## 自动升级

当检测到流量超过当前级别阈值时：
1. 系统会连续 `upgrade_threshold` 次检测到流量超标
2. 自动升级到更高一级别的策略
3. 应用新的牵引规则

## 规则过期

- 每条规则会持续跟踪最后一次检测到攻击的时间
- 攻击停止 30 秒后自动撤销规则

## Dry-Run 模式

在 dry-run 模式下：
- 不会连接 ExaBGP
- 不会实际下发 BGP 规则
- 不会写入 InfluxDB
- 所有操作只记录日志

## 故障排查

### 日志格式

系统使用 JSON 格式日志，便于后续分析和处理。

### 常见问题

1. **ExaBGP 连接失败**
   - 检查 `exabgp_socket` 路径是否正确
   - 确认 ExaBGP 进程正在运行

2. **InfluxDB 连接失败**
   - 确认 InfluxDB 服务运行
   - 检查 token 和 bucket 配置

3. **ML 模型准确率下降**
   - 使用 `--retrain` 参数重新训练模型
   - 检查是否有新的攻击类型出现

4. **升级规则没有生效**
   - 确认 sFlow 采样率配置正确
   - 检查流量计算是否准确

## 许可证

MIT License
