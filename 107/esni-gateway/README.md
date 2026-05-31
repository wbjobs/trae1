# ESNI Gateway - 高性能TLS SNI代理网关

一个用Rust编写的高性能加密SNI（ECH）代理网关，支持在不解密TLS内容的情况下识别目标域名并进行路由决策。

## 功能特性

- ✅ **TLS 1.3 ClientHello解析** - 提取SNI和ECH扩展
- ✅ **TLS透传** - 不终止TLS连接，仅解析SNI
- ✅ **SNI路由决策** - 支持黑名单和白名单模式，通配符匹配
- ✅ **DNSSEC验证** - 防止DNS劫持
- ✅ **Prometheus Metrics** - 完整的监控指标体系
- ✅ **多核并发** - 基于Tokio的高性能异步处理
- ✅ **高性能** - 目标50万cps处理能力
- ✅ **ECH解密** - 支持解密Encrypted ClientHello扩展
- ✅ **ECH密钥轮换** - 支持每小时自动密钥轮换
- ✅ **ECH失败回退** - ECH解密失败时可回退到传统SNI
- ✅ **ECH失败日志** - 单独记录ECH解密失败的连接
- ✅ **TLS指纹识别** - 基于CNN识别目标应用（Google/Facebook/Twitter等）
- ✅ **指纹缓存** - LRU缓存避免重复计算
- ✅ **在线学习** - 用户反馈修正误判

## 快速开始

### 编译

```bash
cargo build --release
```

### 运行

#### 基本运行

```bash
./target/release/esni-gateway --listen 0.0.0.0:443
```

#### 使用黑名单

```bash
./target/release/esni-gateway --block-list block_list.txt
```

#### 白名单模式

```bash
./target/release/esni-gateway --whitelist-mode --allow-list allow_list.txt
```

#### 启用ECH解密

```bash
./target/release/esni-gateway --ech-key ech_keys.json --ech-failure-log ech_failures.log
```

#### 启用TLS指纹识别

```bash
./target/release/esni-gateway \
  --enable-fingerprint \
  --fingerprint-db models/tls_fingerprint_model.json \
  --app-database models/app_database.json \
  --fingerprint-threshold 0.5
```

#### 完整配置

```bash
./target/release/esni-gateway \
  --listen 0.0.0.0:443 \
  --workers 8 \
  --block-list block_list.txt \
  --metrics-addr 127.0.0.1:9090 \
  --dnssec-verify \
  --ech-key ech_keys.json \
  --ech-failure-log ech_failures.log \
  --ech-fallback-enabled true \
  --enable-fingerprint \
  --fingerprint-db models/tls_fingerprint_model.json \
  --app-database models/app_database.json \
  --fingerprint-threshold 0.5 \
  --fingerprint-cache-size 10000 \
  --log-level info
```

## 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--listen` | 监听地址 | `0.0.0.0:443` |
| `--workers` | 工作线程数 | `4` |
| `--block-list` | 黑名单文件路径 | - |
| `--allow-list` | 白名单文件路径 | - |
| `--whitelist-mode` | 启用白名单模式 | `false` |
| `--dnssec-verify` | 启用DNSSEC验证 | `false` |
| `--metrics-addr` | Prometheus metrics地址 | `127.0.0.1:9090` |
| `--log-level` | 日志级别 | `info` |
| `--buffer-size` | 缓冲区大小 | `8192` |
| `--timeout-secs` | 连接超时时间（秒） | `30` |
| `--enable-esni` | 启用ESNI支持 | `false` |
| `--ech-key` | ECH密钥文件路径 | - |
| `--ech-failure-log` | ECH失败日志文件路径 | - |
| `--ech-rotation-interval` | ECH密钥轮换间隔（秒） | `3600` |
| `--ech-fallback-enabled` | 启用ECH失败回退 | `true` |
| `--fingerprint-db` | TLS指纹模型文件路径 | - |
| `--enable-fingerprint` | 启用TLS指纹识别 | `false` |
| `--fingerprint-threshold` | 指纹识别置信度阈值 | `0.5` |
| `--fingerprint-cache-size` | 指纹缓存大小 | `10000` |
| `--fingerprint-use-cache` | 启用指纹缓存 | `true` |
| `--app-database` | 应用数据库文件路径 | - |

## 配置文件格式

### 黑名单/白名单格式

```
# 注释以 # 开头
# 支持通配符 *.example.com

malware.com
phishing.net
*.evil.com
```

### ECH密钥格式

```json
{
    "key_pairs": [
        {
            "key_id": "base64_encoded_key_id",
            "private_key": "base64_encoded_private_key",
            "public_key": "base64_encoded_public_key",
            "valid_from": 1717200000,
            "valid_until": 1717286400,
            "kdf_id": 1,
            "aead_id": 2,
            "maximum_name_length": 255,
            "public_name": "public.example.com"
        }
    ],
    "key_rotation_interval": 3600,
    "fallback_enabled": true
}
```

### 应用数据库格式

```json
[
  {
    "id": 0,
    "name": "google",
    "category": "search",
    "confidence_threshold": 0.5
  },
  {
    "id": 1,
    "name": "facebook",
    "category": "social",
    "confidence_threshold": 0.5
  }
]
```

## Prometheus Metrics

访问 `http://localhost:9090/metrics` 获取指标：

### 连接指标
- `esni_connections_total` - 总连接数
- `esni_connections_active` - 活跃连接数
- `esni_request_duration_seconds` - 请求处理时长

### SNI指标
- `esni_sni_parse_success` - SNI解析成功次数
- `esni_sni_parse_failure` - SNI解析失败次数
- `esni_requests_total{domain="..."}` - 各域名请求总数
- `esni_requests_blocked{domain="..."}` - 各域名阻断次数

### DNSSEC指标
- `esni_dnssec_verifications` - DNSSEC验证次数
- `esni_dnssec_failures` - DNSSEC验证失败次数

### ECH指标
- `ech_attempts` - ECH解密尝试次数
- `ech_success` - ECH解密成功次数
- `ech_failure{error_type="..."}` - ECH解密失败次数（按错误类型）
- `ech_decryption_time_seconds` - ECH解密耗时
- `ech_fallback_total` - ECH回退次数

### TLS指纹指标
- `fingerprint_extractions_total` - TLS指纹提取次数
- `fingerprint_predictions_total{app="..."}` - 各应用识别次数
- `fingerprint_cache_hits_total` - 指纹缓存命中次数
- `fingerprint_cache_misses_total` - 指纹缓存未命中次数
- `fingerprint_extraction_failures_total` - 指纹提取失败次数
- `fingerprint_low_confidence_total` - 低置信度预测次数
- `fingerprint_confidence` - 指纹识别置信度分布

## TLS指纹识别功能

### 工作原理

当无法获取SNI时（如ECH加密或缺失），TLS指纹识别通过分析ClientHello中的特征来识别目标应用：

1. **特征提取**：从TLS握手包中提取特征向量
   - 支持的密码套件列表（300维one-hot）
   - 支持的扩展列表（100维one-hot）
   - 椭圆曲线列表（30维one-hot）
   - 签名算法列表（50维one-hot）
   - 压缩方法列表（10维one-hot）
   - TLS版本和会话ID长度

2. **CNN推理**：使用1D CNN模型进行分类
   - 3层卷积层 + ReLU激活 + 最大池化
   - 2层全连接层 + Softmax输出
   - 支持9个应用类别

3. **缓存机制**：LRU缓存避免重复计算
   - 可配置缓存大小（默认10000条）
   - 缓存命中率监控

4. **在线学习**：用户反馈修正误判
   - 记录预测结果和置信度
   - 用户可提交修正反馈
   - 自动清理过期数据

### 支持的应用

- Google（搜索）
- Facebook（社交）
- Twitter（社交）
- Netflix（流媒体）
- Amazon（电商）
- Apple（技术）
- Microsoft（技术）
- Cloudflare（基础设施）
- Unknown（未知）

### 性能目标

- 连接处理能力：500,000+ CPS
- ECH解密性能：100,000+ CPS
- 指纹识别性能：100,000+ CPS
- 指纹缓存命中率：>90%
- 指纹识别准确率：>85%
- 处理性能影响：<5%

### 模型训练

使用提供的Python脚本训练自定义模型：

```bash
# 训练模型
python train_model.py

# 模型将保存为 tls_fingerprint_model.json
```

## ECH功能详解

### 工作原理

1. 网关接收客户端TLS连接
2. 解析ClientHello，检查是否包含ECH扩展
3. 如果包含ECH，使用对应密钥尝试解密
4. 从解密的InnerClientHello中提取真实SNI
5. 如果ECH解密失败，根据配置决定是否回退到传统SNI
6. 如果无法获取SNI，使用TLS指纹识别
7. 根据SNI或指纹进行路由决策（黑名单/白名单检查）
8. 透传整个加密流到后端服务器

### ECH密钥轮换

- 密钥设置有效期（valid_from, valid_until）
- 支持加载多个密钥，覆盖不同时间区间
- 自动清理过期密钥
- 可配置轮换间隔（默认3600秒）

### 回退机制

当ECH解密失败时：
- 如果启用回退，尝试使用传统SNI
- 如果传统SNI也缺失，尝试使用TLS指纹识别
- 如果所有方法都失败，根据配置决定是否阻断连接
- 所有ECH失败都会记录到指定日志文件（如果配置）

## 架构设计

```
┌─────────────┐
│   Client    │
└──────┬──────┘
       │ TLS 1.3 (with ECH)
       ▼
┌─────────────────────────────────┐
│      ESNI Gateway               │
│  ┌──────────────────────────┐  │
│  │  TLS Parser (SNI/ECH)    │  │
│  └──────────┬───────────────┘  │
│             ▼                   │
│  ┌──────────────────────────┐  │
│  │   ECH Decryptor          │  │
│  └──────────┬───────────────┘  │
│             ▼                   │
│  ┌──────────────────────────┐  │
│  │  TLS Fingerprint (CNN)    │  │
│  └──────────┬───────────────┘  │
│             ▼                   │
│  ┌──────────────────────────┐  │
│  │   Router (Block/Allow)   │  │
│  └──────────┬───────────────┘  │
│             ▼                   │
│  ┌──────────────────────────┐  │
│  │  DNSSEC Verifier         │  │
│  └──────────┬───────────────┘  │
│             ▼                   │
│  ┌──────────────────────────┐  │
│  │  TLS Passthrough Proxy   │  │
│  └──────────┬───────────────┘  │
└─────────────┼───────────────────┘
              │ TLS (encrypted)
              ▼
       ┌─────────────┐
       │   Backend   │
       └─────────────┘
```

## 性能优化

- **零拷贝解析** - 最小化内存分配
- **异步I/O** - Tokio多任务并发
- **连接池** - 复用后端连接
- **无锁数据结构** - 高并发路由表
- **SIMD优化** - 可选的加速解析
- **LRU缓存** - 指纹识别缓存减少重复计算

## 测试

```bash
# 运行单元测试
cargo test

# 运行性能测试
cargo test --release -- --nocapture test_performance

# 训练TLS指纹模型
python train_model.py
```

## 安全注意事项

1. 网关不终止TLS连接，仅解析ClientHello中的SNI
2. ECH解密需要持有服务端的ECH私钥
3. DNSSEC验证需要可信的DNS解析器
4. 建议在生产环境中使用防火墙限制访问
5. ECH密钥应妥善保管，定期轮换
6. TLS指纹模型应定期更新以提高准确率
7. 用户反馈数据可用于改进模型

## Docker部署

```bash
# 使用docker-compose
docker-compose up -d

# 构建镜像
docker build -t esni-gateway .

# 运行容器
docker run -p 443:443 -p 9090:9090 \
  -v ./ech_keys.json:/etc/esni-gateway/ech_keys.json \
  -v ./models:/etc/esni-gateway/models \
  esni-gateway
```

## 项目结构

```
esni-gateway/
├── src/
│   ├── main.rs                 # 主入口
│   ├── config.rs               # 配置管理
│   ├── proxy.rs                # TLS透传代理
│   ├── tls_parser.rs           # TLS解析器
│   ├── router.rs               # 路由决策
│   ├── dnssec.rs               # DNSSEC验证
│   ├── ech.rs                  # ECH解密
│   ├── fingerprint.rs          # TLS指纹提取
│   ├── cnn_model.rs            # CNN推理引擎
│   ├── fingerprint_manager.rs  # 指纹管理器
│   ├── online_learning.rs       # 在线学习
│   ├── metrics.rs              # Prometheus指标
│   ├── connection_pool.rs      # 连接池
│   ├── worker_pool.rs          # 工作线程池
│   └── benchmark.rs            # 性能测试
├── models/
│   ├── tls_fingerprint_model.json  # CNN模型权重
│   └── app_database.json           # 应用数据库
├── tests/
│   └── integration_tests.rs    # 集成测试
├── train_model.py              # 模型训练脚本
├── Cargo.toml
└── README.md
```

## 许可证

MIT License
