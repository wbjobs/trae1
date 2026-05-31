# Malware Scanner Service

基于 YARA 规则的恶意文件扫描服务，使用 Python + FastAPI + PostgreSQL 构建。

## 功能特性

- 文件上传扫描（最大 50MB）
- 125+ 预置 YARA 规则（涵盖木马、勒索软件、后门、蠕虫等）
- 多文件批量扫描（最多 10 个文件并行）
- MD5 去重（相同文件直接返回历史结果）
- PostgreSQL 持久化存储
- RESTful API 接口
- CLI 命令行工具

## 项目结构

```
.
├── app/
│   ├── __init__.py
│   ├── main.py              # FastAPI 应用入口
│   ├── config.py            # 配置管理
│   ├── scanner/
│   │   ├── __init__.py
│   │   ├── yara_rules.py    # YARA 规则加载器
│   │   └── engine.py        # 扫描引擎核心
│   ├── db/
│   │   ├── __init__.py
│   │   ├── models.py        # SQLAlchemy 模型
│   │   └── crud.py          # 数据库操作
│   └── api/
│       ├── __init__.py
│       └── routes.py        # API 路由
├── cli/
│   └── scan_cli.py         # CLI 工具
├── rules/                   # YARA 规则文件
│   ├── malware.yar         # 恶意软件特征规则
│   ├── suspicious.yar      # 可疑行为规则
│   ├── packer.yar          # 壳/打包器规则
│   ├── ransomware.yar      # 勒索软件规则
│   └── trojan.yar          # 木马规则
├── requirements.txt
└── .env.example
```

## 安装部署

### 1. 安装依赖

```bash
pip install -r requirements.txt
```

### 2. 配置环境变量

创建 `.env` 文件或设置环境变量：

```bash
SCANNER_DATABASE_URL=postgresql://postgres:password@localhost:5432/malware_scanner
SCANNER_MAX_FILE_SIZE_MB=50
SCANNER_MAX_PARALLEL_FILES=10
SCANNER_YARA_RULES_DIR=rules
```

### 3. 初始化数据库

```bash
# 确保 PostgreSQL 已启动并创建数据库
createdb malware_scanner
```

### 4. 启动服务

```bash
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

## API 接口

### 扫描单个文件

```bash
POST /api/v1/scan
Content-Type: multipart/form-data

curl -X POST -F "file=@test.exe" http://localhost:8000/api/v1/scan
```

### 批量扫描

```bash
POST /api/v1/scan/batch
Content-Type: multipart/form-data

curl -X POST \
  -F "files=@file1.exe" \
  -F "files=@file2.exe" \
  http://localhost:8000/api/v1/scan/batch
```

### 查询扫描结果

```bash
GET /api/v1/scan/result/{md5}

curl http://localhost:8000/api/v1/scan/result/d41d8cd98f00b204e9800998ecf8427e
```

### 扫描历史

```bash
GET /api/v1/scan/history?skip=0&limit=50&severity=high

curl http://localhost:8000/api/v1/scan/history
```

### 统计信息

```bash
GET /api/v1/stats

curl http://localhost:8000/api/v1/stats
```

### 健康检查

```bash
GET /api/v1/health

curl http://localhost:8000/api/v1/health
```

## CLI 工具使用

```bash
# 扫描单个文件
python cli/scan_cli.py scan test.exe

# 批量扫描多个文件
python cli/scan_cli.py scan file1.exe file2.exe file3.exe

# 查看扫描结果
python cli/scan_cli.py lookup d41d8cd98f00b204e9800998ecf8427e

# 查看扫描历史
python cli/scan_cli.py history --limit 20

# 查看统计信息
python cli/scan_cli.py stats

# 健康检查
python cli/scan_cli.py health

# 指定服务地址
python cli/scan_cli.py --url http://scanner.example.com:8000 scan test.exe
```

## API 响应示例

```json
{
  "file_md5": "d41d8cd98f00b204e9800998ecf8427e",
  "file_name": "test.exe",
  "file_size": 123456,
  "matched_rules": [
    {
      "rule_name": "Suspicious_Behavior_001",
      "rule_description": "检测可疑行为",
      "severity": "medium",
      "category": "suspicious",
      "offset": 1234,
      "matched_string": "..."
    }
  ],
  "scan_time": "2024-01-01T00:00:00",
  "total_matches": 1,
  "highest_severity": "medium",
  "cached": false
}
```

## 规则分类

| 类别 | 文件 | 规则数量 | 说明 |
|------|------|---------|------|
| malware | malware.yar | 25 | 恶意软件通用特征 |
| suspicious | suspicious.yar | 25 | 可疑行为检测 |
| packer | packer.yar | 25 | 壳/打包器检测 |
| ransomware | ransomware.yar | 25 | 勒索软件检测 |
| trojan | trojan.yar | 25 | 木马检测 |

## 严重级别

- `clean`: 无威胁
- `low`: 低风险
- `medium`: 中等风险
- `high`: 高风险
- `critical`: 严重威胁

## License

MIT
