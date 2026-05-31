# SMB 3.0 加密代理工具

一个用C语言开发的SMB 3.0加密代理工具，将本地目录通过SMB 3.0协议暴露给远程客户端，强制启用AES-128-GCM加密，并针对公网高延迟环境进行了专门优化，同时集成了实时防勒索检测功能。

## 功能特性

- **SMB 3.0协议支持**: 兼容Windows/Linux/macOS的标准SMB客户端
- **AES-128-GCM加密**: 强制启用SMB 3.0加密特性，确保传输安全
- **用户认证**: 支持NTLMv2/Kerberos认证机制
- **多用户支持**: 最多支持10个并发会话
- **访问日志**: 所有访问操作记录到本地SQLite数据库
- **性能测试**: 内置性能测试模式，测试连续读写和随机IO性能
- **高延迟优化**: 针对公网环境的信用窗口协商优化（默认8192 credits）
- **LRU缓存**: 内置64MB LRU读写缓存，提升重复访问性能
- **防勒索检测**: 实时监控文件熵值变化，检测加密勒索行为
- **诱饵文件**: 部署伪装文件，早期检测勒索攻击
- **行为学习**: 7天用户行为基线自学习，异常行为自动告警

## 项目结构

```
.
├── Makefile              # Linux/macOS 编译配置
├── Makefile.win          # Windows 编译配置
├── common.h              # 公共定义
├── main.c                # 主程序入口
├── smb_server.h/c        # SMB服务器核心功能
├── crypto.h/c            # AES-128-GCM加密模块
├── auth.h/c              # 用户认证模块
├── session.h/c           # 会话管理模块（含信用机制）
├── lru_cache.h/c         # LRU缓存模块
├── entropy.h/c           # Shannon熵计算模块
├── ransomware_detector.h/c # 勒索检测核心模块
├── behavior_learning.h/c  # 用户行为学习模块
├── logger.h/c            # SQLite访问日志模块
└── bench.h/c             # 性能测试模块
```

## 依赖库

编译前需要安装以下开发库：

### Ubuntu/Debian
```bash
sudo apt-get install -y libssl-dev libsqlite3-dev
```

### CentOS/RHEL
```bash
sudo yum install -y openssl-devel sqlite-devel
```

### macOS (Homebrew)
```bash
brew install openssl sqlite
```

### Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-sqlite3
```

## 编译

```bash
make
```

编译后生成可执行文件 `smb_encrypt_proxy`。

## 使用方法

### 基本命令行参数

| 参数 | 说明 | 默认值 | 必需 |
|------|------|--------|------|
| `--smb-share <name>` | 指定SMB共享名称 | - | 是 |
| `--path <path>` | 指定要共享的本地路径 | - | 是 |
| `--encrypt` | 强制启用AES-128-GCM加密 | 关闭 | 否 |
| `--username <user>` | 指定认证用户名 | 无 | 否 |
| `--password <pass>` | 指定认证密码 | 无 | 否 |
| `--credit <num>` | 设置最大信用值（512-65535） | 8192 | 否 |
| `--cache-size <size>` | 设置缓存大小（MB） | 64 | 否 |
| `--ransomware-protect` | 启用勒索软件检测和防护 | 关闭 | 否 |
| `--decoy-files` | 部署诱饵文件用于早期检测 | 关闭 | 否 |
| `--entropy-threshold <v>` | 设置熵值阈值（0.0-1.0） | 0.9 | 否 |
| `--no-auto-quarantine` | 禁用检测后自动隔离 | 开启 | 否 |
| `--bench` | 运行性能测试模式 | 关闭 | 否 |
| `--log <path>` | 指定SQLite日志文件路径 | smb_access.log | 否 |
| `--help` | 显示帮助信息 | - | 否 |

### 示例用法

#### 1. 启动SMB加密共享（推荐配置，优化公网性能）
```bash
sudo ./smb_encrypt_proxy --smb-share myshare --path /data/share \
    --encrypt --username admin --password secret123 \
    --credit 8192 --cache-size 64 \
    --ransomware-protect --decoy-files
```

#### 2. 启动带用户认证的加密共享
```bash
sudo ./smb_encrypt_proxy --smb-share myshare --path /data/share \
    --encrypt --username admin --password secret123
```

#### 3. 自定义高延迟优化参数
```bash
sudo ./smb_encrypt_proxy --smb-share myshare --path /data/share \
    --encrypt --credit 16384 --cache-size 128
```

#### 4. 启用完整防勒索保护
```bash
sudo ./smb_encrypt_proxy --smb-share myshare --path /data/share \
    --encrypt --ransomware-protect --decoy-files \
    --entropy-threshold 0.85
```

#### 5. 运行性能测试（带加密）
```bash
./smb_encrypt_proxy --bench --encrypt --path /tmp \
    --credit 8192 --cache-size 64
```

## 防勒索检测说明

### 检测原理

系统通过以下多层次检测机制识别勒索攻击：

#### 1. 熵值检测（Entropy Detection）
- 实时计算文件写入内容的Shannon熵
- 4KB滑动窗口，持续监控熵值变化
- 熵值>0.9表示数据被加密（接近随机）
- 正常文本熵值约0.5-0.7，加密数据>0.9

#### 2. 扩展名检测（Extension Detection）
- 监控文件重命名操作
- 检测可疑扩展名：`.enc`, `.locked`, `.crypt`, `.encrypted`, `.ransom`等
- 熵值升高+扩展名改变 = 高度可疑

#### 3. 诱饵文件（Decoy Files）
- 部署伪装成重要文件的诱饵文档
- 诱饵文件被修改/删除时立即告警
- 用于检测勒索软件的批量加密行为

#### 4. 行为学习（Behavior Learning）
- 7天建立用户行为基线
- 监控IO模式、熵值分布、文件操作频率
- 偏离基线3倍标准差以上视为异常

### 检测响应

当检测到勒索行为时，系统执行以下响应：

1. **告警日志**: 写入 `ransomware_alerts.log`
2. **会话隔离**: 阻断可疑客户端会话
3. **IP隔离**: 将可疑IP加入隔离列表
4. **共享冻结**: 切换为只读模式，防止进一步写入
5. **证据保留**: 保存前10分钟访问日志用于追溯

### 熵值说明

| 熵值范围 | 数据类型 | 风险等级 |
|----------|----------|----------|
| 0.0 - 0.3 | 高度结构化数据（压缩包/图片） | 低 |
| 0.3 - 0.6 | 普通文档/文本 | 低 |
| 0.6 - 0.8 | 代码/二进制文件 | 中 |
| 0.8 - 0.9 | 加密/压缩数据 | 高 |
| 0.9 - 1.0 | 随机数据（疑似加密） | **极高** |

### 可疑扩展名列表

系统默认监控以下勒索软件常用扩展名：

```
.enc, .locked, .crypt, .encrypted, .ransom,
.locked, .crypto, .aes, .rsa, .bitcoin
```

可以通过修改 `ransomware_detector.c` 中的 `default_suspicious_extensions` 数组添加更多扩展名。

## 性能优化说明

### 高延迟网络问题

在公网高延迟环境（如200ms RTT）中，SMB默认的信用窗口（512 credits）会导致吞吐量严重下降：

- **默认窗口**: 512 credits = 512 * 64KB = 32MB 在途数据
- **吞吐量限制**: 32MB / 0.2s = 160 MB/s（理论峰值）
- **实际影响**: 由于协议开销，实际吞吐量可能低于5 MB/s

### 信用窗口优化

通过 `--credit` 参数提高最大信用值：

| 信用值 | 在途数据量 | 200ms RTT理论吞吐 | 适用场景 |
|--------|-----------|-------------------|----------|
| 512 | 32 MB | 160 MB/s | 局域网 |
| 2048 | 128 MB | 640 MB/s | 城域网 |
| 8192 | 512 MB | 2.5 GB/s | 公网（默认） |
| 16384 | 1 GB | 5 GB/s | 高延迟公网 |
| 32768 | 2 GB | 10 GB/s | 极端延迟环境 |
| 65535 | 4 GB | 20 GB/s | 最大支持 |

### LRU缓存优化

通过 `--cache-size` 参数设置LRU缓存大小：

- **读取缓存**: 缓存热点文件数据，重复访问直接从内存返回
- **写入缓存**: 延迟写入，合并小写入操作
- **缓存统计**: 会话关闭时显示缓存命中率

### 推荐配置

#### 局域网（<1ms RTT）
```bash
--credit 512 --cache-size 32
```

#### 城域网（10-50ms RTT）
```bash
--credit 4096 --cache-size 64
```

#### 公网（100-200ms RTT）
```bash
--credit 8192 --cache-size 128
```

#### 跨洋网络（200-500ms RTT）
```bash
--credit 32768 --cache-size 256
```

## 客户端访问

### Windows
在资源管理器地址栏输入：
```
\\<服务器IP>\myshare
```

### Linux
```bash
mount -t cifs //<服务器IP>/myshare /mnt/myshare -o username=admin,password=secret123,vers=3.0,seal
```

### macOS
在Finder中按 Cmd+K，输入：
```
smb://<服务器IP>/myshare
```

## 访问日志

所有访问操作都会记录到SQLite数据库。可以通过以下SQL查询访问记录：

```sql
-- 查询所有访问记录
SELECT * FROM access_logs ORDER BY timestamp DESC;

-- 查询特定用户的访问
SELECT * FROM access_logs WHERE username = 'admin';

-- 查询特定IP的访问
SELECT * FROM access_logs WHERE client_ip = '192.168.1.100';

-- 统计各种操作的次数
SELECT action, COUNT(*) as count FROM access_logs GROUP BY action;
```

## 安全告警日志

勒索检测告警保存在 `ransomware_alerts.log`：

```
[2026-05-26 10:30:45] ALERT: High entropy write detected: 0.95 for file: report.docx by admin@192.168.1.100
[2026-05-26 10:30:46] ALERT: Client quarantined: 192.168.1.100
[2026-05-26 10:30:47] ALERT: Share frozen - entering read-only mode
```

## 性能测试

性能测试模式会测试以下指标：
- 连续读速度 (MB/s)
- 连续写速度 (MB/s)
- 随机读IOPS
- 随机写IOPS
- 各操作的平均延迟 (ms)

测试文件大小为100MB，随机IO测试执行1000次操作。

## 安全说明

1. **加密传输**: 使用 `--encrypt` 参数强制启用AES-128-GCM加密，确保数据在传输过程中不被窃取或篡改。
2. **用户认证**: 建议始终设置用户名和密码，避免匿名访问。
3. **防火墙**: 确保只开放445端口给信任的网络。
4. **权限管理**: 确保共享目录的本地文件权限设置正确。
5. **防勒索保护**: 使用 `--ransomware-protect` 启用实时勒索检测，保护数据安全。

## 注意事项

1. 需要root权限绑定445端口（SMB标准端口）
2. 确保防火墙已开放445端口
3. 最多支持10个并发会话
4. 建议在生产环境中使用强密码
5. 缓存会占用内存，请根据实际可用内存调整 `--cache-size`
6. 启用防勒索检测会略微增加CPU开销

## 故障排除

### 无法绑定端口445
如果445端口被占用（如系统已运行Samba服务），需要先停止相关服务：
```bash
sudo systemctl stop smbd
sudo systemctl stop nmbd
```

### 公网吞吐量低
1. 检查 `--credit` 值是否足够大（建议8192或更高）
2. 增加 `--cache-size` 提升缓存命中率
3. 检查网络带宽和丢包率

### 勒索检测误报
1. 调整 `--entropy-threshold` 到合适的值（0.8-0.95）
2. 压缩包和加密文件可能触发误报
3. 使用 `--no-auto-quarantine` 先观察告警再手动处理

### 客户端无法连接
1. 检查防火墙设置
2. 确认服务器IP地址正确
3. 检查SMB版本是否为3.0或以上
4. 查看服务器日志输出

## 许可证

本项目仅供学习和研究使用。
