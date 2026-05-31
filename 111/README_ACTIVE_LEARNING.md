# MySQL Firewall - 主动学习模式

## 概述

本项目实现了基于主动学习的SQL注入检测系统，通过机器学习模型和沙箱验证持续提升检测准确率。

## 工作原理

```
Normal SQL → 检测引擎 → 低分数 → 放行
Suspicious SQL → 检测引擎 → 中分数 → 沙箱执行 → 学习引擎 → 模型更新
Malicious SQL → 检测引擎 → 高分数 → 拦截
```

### 检测流程

```
           ┌─────────────────┐
   SQL ───▶│  检测引擎       │
           └────────┬────────┘
                    │
           ┌────────▼────────┐
           │  分数计算       │
           └────────┬────────┘
                    │
      ┌─────────────┼─────────────┐
      ▼             ▼             ▼
  分数 < 30    30 ≤ 分数 < 60   分数 ≥ 60
      │             │             │
      ▼             ▼             ▼
    放行      沙箱验证+学习      拦截
                    │
                    ▼
              模型加权更新
```

## 新增命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--learn` | 启用主动学习模式 | 关闭 |
| `--sandbox-host HOST` | 沙箱MySQL主机 | 127.0.0.1 |
| `--sandbox-port PORT` | 沙箱MySQL端口 | 3307 |
| `--sandbox-user USER` | 沙箱MySQL用户 | sandbox |
| `--sandbox-pass PASS` | 沙箱MySQL密码 | sandbox_pass |
| `--sandbox-db DB` | 沙箱MySQL数据库 | sandbox_db |
| `--export FILE` | 学习结束后导出规则 | - |
| `--import FILE` | 导入已学习的规则 | - |

## 检测阈值配置

```c
#define DETECTION_THRESHOLD_BLOCK 60      // 拦截阈值
#define DETECTION_THRESHOLD_SUSPICIOUS 30  // 存疑阈值
```

- **分数 ≥ 60**: 直接拦截
- **30 ≤ 分数 < 60**: 标记为存疑，送沙箱验证
- **分数 < 30**: 正常放行

## 沙箱环境

### 资源限制
- **CPU**: 1核 (通过cgroups/rlimit)
- **内存**: 256MB (RLIMIT_AS)
- **超时**: 5秒 (SIGKILL)
- **进程数**: 最多10个
- **coredump**: 禁用

### MySQL沙箱部署建议

```sql
-- 创建沙箱数据库
CREATE DATABASE sandbox_db;

-- 创建受限用户
CREATE USER 'sandbox'@'localhost' IDENTIFIED BY 'sandbox_pass';

-- 最小权限原则
GRANT SELECT, INSERT, UPDATE, DELETE ON sandbox_db.* TO 'sandbox'@'localhost';
REVOKE FILE ON *.* FROM 'sandbox'@'localhost';
REVOKE SUPER ON *.* FROM 'sandbox'@'localhost';
REVOKE PROCESS ON *.* FROM 'sandbox'@'localhost';

-- 创建模拟表
CREATE TABLE sandbox_db.users (id INT AUTO_INCREMENT PRIMARY KEY, username VARCHAR(64), email VARCHAR(128));
CREATE TABLE sandbox_db.products (id INT, name VARCHAR(128), price DECIMAL(10,2));

-- 插入测试数据
INSERT INTO sandbox_db.users VALUES (1, 'admin', 'admin@test.com'), (2, 'user1', 'user1@test.com');
INSERT INTO sandbox_db.products VALUES (1, 'Product A', 99.99), (2, 'Product B', 199.99);
```

### 安全加固

```bash
# AppArmor 配置 (示例)
cat > /etc/apparmor.d/usr.sbin.mysqld-sandbox << 'EOF'
/usr/sbin/mysqld {
  # 只允许访问沙箱数据目录
  /var/lib/mysql-sandbox/** rwk,
  /var/run/mysqld-sandbox/** rwk,
  # 禁止系统命令执行
  deny /bin/** x,
  deny /usr/bin/** x,
  deny /sbin/** x,
}
EOF
```

## 使用示例

### 1. 学习模式（训练期7天）

```bash
# 启动学习模式，使用本地沙箱
./mysql_firewall --learn \
  --sandbox-host 127.0.0.1 \
  --sandbox-port 3307 \
  --sandbox-user sandbox \
  --sandbox-pass secure_pass \
  --sandbox-db sandbox_db \
  --export /etc/mysql_firewall/learned_rules.conf \
  --debug
```

### 2. 学习期结束后导入生产环境

```bash
# 导入已学习的规则
./mysql_firewall --import /etc/mysql_firewall/learned_rules.conf
```

### 3. 带沙箱验证的生产模式

```bash
./mysql_firewall --learn \
  --sandbox-host 10.0.0.100 \
  --sandbox-port 3307 \
  --import /etc/mysql_firewall/learned_rules.conf \
  --export /var/log/mysql_firewall_weekly_rules.conf
```

## 学习规则导出格式

```ini
# MySQL Firewall Learned Rules
# Exported: Tue May 26 10:30:45 2026
# Total rules: 156

[metadata]
learning_period_days = 7
start_time = 1779726919
total_learned = 156
total_verified = 892
total_false_positives = 3
total_false_negatives = 1
learning_rate = 0.1000

[rules]
# format: pattern | type | weight | hits | false_positives | false_negatives | precision | recall
'information_schema|2|95|127|2|0|0.9845|1.0000
' OR '1'='1'|1|100|89|0|0|1.0000|1.0000
UNION SELECT|1|90|67|1|0|0.9853|1.0000
LOAD_FILE|2|95|34|0|0|1.0000|1.0000
xp_cmdshell|2|100|12|0|0|1.0000|1.0000
SELECT * FROM users|1|25|1156|156|2|0.8810|0.9983
```

## 统计信息

```
=== MySQL Firewall Statistics ===
Total Packets:        1,234,567
Total Inspections:    45,678
Total Suspicious:     892
Total Blocks:         1,234
Total Allows:         44,444
Regex Matches:        456
Syntax Matches:       789
Learning Matches:     123
Sandbox Verified:     892
================================

=== Learning Status ===
Learning Mode:        ON
Total Learned:        156
Total Verified:       892
Rules Count:          156
Learning Period:      7 days
========================
```

## 日志格式

### 拦截日志
```
[2026-05-26 10:30:45] BLOCK [LEARNING] 192.168.1.100:54321 -> 10.0.0.50:3306 | SQL: SELECT * FROM users WHERE id=1 OR 1=1 | Reason: LEARNING_SANDBOX_CONFIRMED | score: 95
```

### 存疑日志
```
[2026-05-26 10:30:46] SUSPICIOUS [score:45] 192.168.1.101:54322 -> 10.0.0.50:3306 | SQL: SELECT * FROM products WHERE id='1' OR 'a'='a' | sandbox: DANGEROUS
```

## 学习算法

### 权重更新公式

```
正样本（确认注入）:
weight = weight * (1 + learning_rate)

负样本（误报）:
weight = weight * (1 - learning_rate)

学习率 learning_rate = 0.1

精确率 Precision = hits / (hits + false_positives)
召回率 Recall = hits / (hits + false_negatives)

最终权重 = weight * precision
```

### 规则淘汰策略

- 权重降为0的规则自动删除
- 精确率 < 0.3 且命中数 > 10 的规则自动删除
- LRU缓存最多保存1000条规则

## 性能指标

| 操作 | 延迟 |
|------|------|
| 快速检测（分数 < 30 或 ≥ 60） | < 2ms |
| 沙箱验证（30 ≤ 分数 < 60） | 5-10ms (含MySQL执行) |
| 模型更新 | < 1ms |
| 规则导入/导出 | < 100ms |

**注意**: 沙箱验证只作用于存疑SQL（约占总流量的1-5%），不会影响正常流量处理性能。

## 高可用部署

### 独立沙箱服务器

```
Production Firewall ─────┐
Production Firewall ─────┼──► Sandbox Cluster (HAProxy)
Production Firewall ─────┘
```

### 规则同步

```bash
# 定时从中央服务器拉取规则
*/30 * * * * root /usr/local/bin/mysql_firewall --import http://rules.example.com/learned_rules.conf 2>&1 >> /var/log/firewall_update.log
```

## 最佳实践

1. **学习期**: 建议在测试环境运行7天学习模式，导出规则后再部署到生产
2. **沙箱隔离**: 沙箱MySQL必须与生产环境物理隔离
3. **最小权限**: 沙箱数据库用户只授予最小必要权限
4. **定期导出**: 每周导出一次学习到的规则
5. **人工审核**: 高风险规则建议人工审核后再应用
6. **白名单**: 可信应用的IP加入白名单，避免误杀

## 风险提示

1. 沙箱环境本身可能成为攻击目标，必须严格隔离
2. 学习到的规则可能存在误报，建议逐步启用
3. 沙箱执行可能会执行真实的注入攻击代码，请确保数据为测试数据
4. 长时间运行沙箱可能会积累真实攻击样本，请定期重置沙箱环境

## 故障排除

### 沙箱连接失败
```
# 测试连接
mysql -h127.0.0.1 -P3307 -usandbox -psandbox_pass sandbox_db

# 检查权限
SHOW GRANTS FOR 'sandbox'@'localhost';
```

### 学习规则未生效
```
# 确认规则文件格式
cat /etc/mysql_firewall/learned_rules.conf | head -20

# 检查日志
grep "Imported" /var/log/mysql_firewall.log
```

### 性能问题
```
# 监控存疑比例
grep "SUSPICIOUS" /var/log/mysql_firewall.log | wc -l

# 如存疑比例过高，可调整阈值
DETECTION_THRESHOLD_SUSPICIOUS = 40
```
