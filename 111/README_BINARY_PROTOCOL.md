# MySQL Firewall - Binary Protocol Support

## 功能概述

本项目实现了一个基于Netfilter的MySQL防火墙，支持**文本协议**和**二进制协议**（PreparedStatement）的SQL注入检测。

## 新增功能

### 1. PreparedStatement 二进制协议解析

- **COM_STMT_PREPARE 阶段**:
  - 捕获SQL模板（如 `INSERT INTO users VALUES (?, ?, ?)`）
  - 关联 statement ID 与 SQL 模板
  - 缓存到LRU缓存中

- **COM_STMT_EXECUTE 阶段**:
  - 从Execute包中提取实际参数值
  - 支持所有MySQL数据类型
  - 重建完整SQL语句进行注入检测

### 2. LRU 语句缓存

- 容量：最多 1000 条语句
- 策略：Least Recently Used（最近最少使用）淘汰
- 实现：哈希表 + 双向链表

### 3. 支持的数据类型

| 类型 | 说明 |
|------|------|
| TINYINT | 1字节整数 |
| SMALLINT | 2字节整数 |
| INT | 4字节整数 |
| BIGINT | 8字节整数 |
| INT24 | 3字节整数 |
| FLOAT | 4字节浮点数 |
| DOUBLE | 8字节浮点数 |
| CHAR/VARCHAR | 字符串类型 |
| TEXT/BLOB | 大文本/二进制 |
| DATE/DATETIME | 日期时间 |
| NULL | 空值 |
| 其他 | 自动转义处理 |

### 4. SQL转义处理

自动转义特殊字符：
- 单引号 `'` → `''`
- 反斜杠 `\` → `\\`
- 换行符 `\n` → `\n`
- 回车符 `\r` → `\r`
- 制表符 `\t` → `\t`
- 控制字符 → 十六进制转义

## 工作流程

```
Client → MySQL Server
    |
    ↓ COM_STMT_PREPARE (sql: "INSERT ... VALUES (?, ?)")
    |
  Firewall: 缓存 stmt_id → SQL模板
    |
    ↓ COM_STMT_EXECUTE (stmt_id, params: [1, 'admin'])
    |
  Firewall:
    1. 查缓存获取SQL模板
    2. 解析二进制参数
    3. 重建完整SQL: "INSERT ... VALUES (1, 'admin')"
    4. 执行注入检测
    5. 拦截或放行
```

## 性能指标

| 操作 | 预期延迟 |
|------|----------|
| 缓存查找 | < 0.1ms |
| 参数解析 | < 0.5ms |
| SQL重建 | < 0.5ms |
| 注入检测 | < 0.5ms |
| **总计** | **< 2ms** |

## 编译安装

```bash
# 安装依赖
apt install libnetfilter-queue-dev libpcre3-dev

# 编译
make

# 运行测试
gcc -o test_binary test_binary_protocol.c mysql_parser.c stmt_cache.c -lpthread
./test_binary
```

## 配置iptables

```bash
# 拦截MySQL流量
iptables -I OUTPUT -p tcp --dport 3306 -j NFQUEUE --queue-num 0
iptables -I INPUT -p tcp --sport 3306 -j NFQUEUE --queue-num 0

# 运行防火墙
./mysql_firewall --debug
```

## 白名单配置

创建 `/etc/mysql_firewall/whitelist.conf`：

```
# 信任的IP地址
192.168.1.100
10.0.0.0/8
172.16.0.0/12
```

## 热加载

```bash
# 重新加载白名单和规则
kill -SIGHUP $(pidof mysql_firewall)
```

## 查看统计

```bash
./mysql_firewall --stats
```

## 日志格式

```
[2026-05-26 10:30:45] BLOCK [REGEX] 192.168.1.100:54321 -> 10.0.0.50:3306 | SQL: SELECT * FROM users WHERE id='1' OR '1'='1' | Reason: ' OR '1'='1' detected
```

## 检测规则

### 正则模式
- `' OR '1'='1` 恒真条件
- `UNION SELECT` 联合查询
- `DROP TABLE` 删除表
- `EXEC(` 存储过程执行
- `xp_` `sp_` 扩展存储过程
- `SLEEP(` 时间盲注
- `LOAD_FILE` 文件读取
- `INTO OUTFILE` 文件写入

### 语法模式
- 多关键字组合检测
- URL编码混合检测
- SQL注释与关键字组合

## 项目结构

```
├── mysql_firewall.c      # 主程序 (NFQUEUE + 事件循环)
├── mysql_firewall.h      # 全局定义
├── mysql_parser.c        # MySQL协议解析 (文本+二进制)
├── mysql_parser.h        # 协议结构定义
├── stmt_cache.c          # PreparedStatement LRU缓存
├── stmt_cache.h          # 缓存接口
├── tcp_reassembly.c      # TCP流重组
├── tcp_reassembly.h      # 流重组接口
├── injection_detect.c    # SQL注入检测引擎
├── injection_detect.h    # 检测接口
├── whitelist.c           # IP白名单管理
├── whitelist.h           # 白名单接口
├── config.h              # 编译配置
├── Makefile              # 编译脚本
└── test_binary_protocol.c # 二进制协议测试
```

## 注意事项

1. 需要root权限运行
2. 建议在测试环境充分验证后部署
3. 性能测试建议使用真实业务流量
4. 日志文件默认: `/var/log/mysql_firewall.log`
