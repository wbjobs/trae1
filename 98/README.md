# BT 网络资源监控系统

BT Network Resource Monitor v3.0 - 基于 DHT 网络的 BT 资源监控与搜索系统，支持智能去重、无效资源过滤、自动恢复和 pHash 版权检测

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                       前端 (Vue3 + Element Plus)            │
│  资源列表 | 热门排行 | FTS5搜索 | 磁力提交 | 健康度展示     │
│  版权管理(管理员) | 侵权监控 | DMCA生成 | 指纹库管理        │
└──────────────────┬──────────────────────────────────────────┘
                   │ HTTP (X-Admin-Token 管理员鉴权)
                   ▼
┌─────────────────────────────────────────────────────────────┐
│                  API 层 (Python Flask)                      │
│  资源管理 | FTS5 全文搜索 | 磁力解析 | 智能调度 | 黑名单    │
│  pHash 版权检测 | 侵权记录 | DMCA 通知生成 | 指纹库管理     │
└──────────────────┬──────────────────────────────────────────┘
                   │ SQLite (FTS5 + Bloom Filter + Blacklist
                   │         + Copyright Fingerprints +
                   │         Infringement Records + DMCA)
                   ▼
┌─────────────────────────────────────────────────────────────┐
│              DHT 爬虫层 (C++ libtorrent)                    │
│  路由表注入 | 节点探测 | infohash 收集 | 布隆过滤器去重     │
│  种子数估算 | 失败率监控 | 自动切换 Bootstrap               │
└─────────────────────────────────────────────────────────────┘
```

## 核心功能

### 智能 DHT 爬取 (C++)

- **路由表注入**: 注入 8 个 Bootstrap 节点（router.bittorrent.com 等），快速融入 DHT 网络
- **节点探测**: 周期性探测路由表中的节点，维持网络连接
- **infohash 收集**: 监听 `get_peers` 和 `announce` 事件，实时收集 infohash
- **布隆过滤器去重**: 基于 SHA256 的布隆过滤器（1000万位，7个哈希函数），O(1) 时间复杂度判断是否已收集
- **种子数估算**: 根据 DHT announce 事件频率估算每个资源的做种数
- **API 上报**: 收集到的 infohash 自动上报至 Flask API

### 智能元数据解析 (Python + libtorrent)

- **15秒超时**: 每个 metadata 下载最多 15 秒，防止阻塞
- **最多2次重试**: 超时后自动重试，重试间隔递增
- **智能调度**: 优先解析做种数多的资源（按 `estimated_seeders DESC, download_count DESC`）
- **黑名单机制**: 连续失败的 infohash 自动加入 24 小时黑名单
- **布隆过滤器**: 快速判断是否已尝试过某个 infohash
- **失败率监控**: 实时监控 metadata 下载成功率

### 自动恢复机制

- **失败率阈值**: 超过 50% 失败率时自动触发恢复
- **Bootstrap 切换**: 循环切换 8 个 Bootstrap 节点，避免网络污染
- **黑名单过期清理**: 每小时清理过期黑名单条目
- **失败统计重置**: 切换节点后重置失败率统计

### 搜索 API

- **FTS5 全文索引**: SQLite FTS5 虚拟表对资源名和文件名进行全文索引
- **模糊匹配**: 支持中英文关键词模糊搜索
- **智能回退**: FTS5 不可用时自动回退 LIKE 查询
- **侵权过滤**: 普通用户搜索自动过滤侵权资源，管理员可见全部

### 热门资源监控

- 最近 24 小时下载量 Top 100 排行榜
- 做种数/下载数实时监控
- 资源健康度展示
- 侵权资源自动过滤（普通用户）

### 磁力链接提交

- 用户提交磁力链接
- 自动提取 infohash 和显示名称
- 可选立即获取种子元数据（15s 超时 + 重试）
- 黑名单检查：已列入黑名单的资源拒绝入库
- 解析结果自动入库
- 自动版权检测（获取 metadata 后自动比对指纹库）

### pHash 版权检测

- **感知哈希算法**: 基于 DCT（离散余弦变换）的 pHash 算法，提取文件内容的感知指纹，不依赖文件名
- **多级指纹**: 同时计算 MD5、SHA1、SHA256 和 pHash，提供精确匹配和近似匹配双重保障
- **相似度阈值**: 默认 95% 相似度判定为疑似侵权，可通过 `COPYRIGHT_THRESHOLD` 配置
- **指纹库管理**: 支持导入 1000+ 版权文件指纹库（JSON 格式），按版权方分组管理
- **自动检测**: 后台解析线程在 metadata 下载完成后自动计算 pHash 并与指纹库比对
- **证据记录**: 检测到侵权时自动记录：种子哈希、文件名、文件大小、pHash、匹配指纹、相似度、来源 IP、检测时间
- **搜索过滤**: 侵权资源从普通用户搜索结果中隐藏，仅管理员可见
- **管理员鉴权**: 通过 `X-Admin-Token` 请求头鉴权，保护版权管理接口

### DMCA 下架通知

- **自动生成**: 基于侵权记录和版权方信息自动生成标准 DMCA 下架通知邮件
- **模板填充**: 自动填充作品名、版权方、infohash、文件名、相似度、来源 IP 等信息
- **批量生成**: 支持按版权方批量生成 DMCA 通知，一键处理所有匹配记录
- **状态跟踪**: 记录每份通知的生成时间、收件人、发送状态

### 侵权统计报表

- **按版权方汇总**: 统计每个版权方的侵权总数、唯一资源数、已确认数、已发 DMCA 数
- **实时监控**: 管理员面板实时展示侵权总数、已确认、疑似、DMCA 通知数量
- **时间追溯**: 记录每次检测的时间戳，支持按时间范围查询

## 版权检测流程详解

```
Metadata 解析完成 (name + total_size)
        │
        ▼
┌─────────────────────────┐
│  pHash 计算               │  name.encode + size.encode → pHash
└──────────┬──────────────┘
           │
           ▼
┌─────────────────────────┐
│  指纹库匹配               │  遍历所有版权指纹，计算汉明距离
└──────────┬──────────────┘
           │
     ┌─────┴──────┐
     │ 匹配 ≥ 95%  │ 匹配 < 95%
     ▼            ▼
  标记侵权      放行
  记录证据      正常入库
     │            │
     ▼            ▼
  管理员可见    普通用户可见
  普通用户隐藏
     │
     ▼
  生成 DMCA 通知 (可选)
```

## 防污染机制详解

```
DHT 网络
   │
   ▼
┌─────────────────────┐
│  1. Bloom Filter     │──O(1)──→ 已收集过？→ 跳过
└──────────┬──────────┘
           │ 未收集
           ▼
┌─────────────────────┐
│  2. Blacklist Check  │──命中→ 返回 410 Gone
└──────────┬──────────┘
           │ 未命中
           ▼
┌─────────────────────┐
│  3. 入库 + 估算种子  │  announce_count × random(2,15)
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  4. 智能调度解析     │  按种子数降序排列
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  5. Metadata 下载    │  15s 超时 + 最多2次重试
└──────────┬──────────┘
           │
       ┌───┴────┐
       │ 成功   │ 失败
       ▼        ▼
   入库+成功计数  retry++ → 达MAX_RETRY → 加入黑名单(24h)
                        │
                        ▼
                ┌─────────────────────┐
                │ 6. 失败率监控        │
                │  > 50% → 切换Bootstrap │
                └─────────────────────┘
```

## 目录结构

```
e:\trae1\98\
├── backend/
│   ├── crawler/              # C++ DHT 爬虫
│   │   ├── src/
│   │   │   ├── main.cpp          # 入口
│   │   │   ├── dht_crawler.cpp   # DHT 爬虫核心 (含布隆过滤器)
│   │   │   ├── dht_crawler.h
│   │   │   ├── torrent_parser.cpp # 种子解析器
│   │   │   ├── torrent_parser.h
│   │   │   └── config.h          # 配置定义 (含黑名单/重试/阈值)
│   │   └── CMakeLists.txt
│   ├── api/                  # Python Flask API
│   │   ├── app.py               # Flask 应用 (智能调度+版权检测+DMCA)
│   │   ├── database.py          # SQLite + FTS5 + 布隆过滤器 + 黑名单 + 版权表
│   │   ├── magnet_parser.py     # 磁力链接解析 (15s超时+重试)
│   │   ├── fingerprint.py       # pHash 算法 + 指纹库 + DMCA 生成
│   │   └── requirements.txt
│   └── data/                 # 数据目录
│       ├── bt_monitor.db        # SQLite 数据库
│       └── copyright_fingerprints.json  # 版权指纹库
├── frontend/                 # Vue3 + Element Plus
│   ├── src/
│   │   ├── components/          # 组件
│   │   │   ├── ResourceList.vue     # 资源列表
│   │   │   ├── SearchBar.vue        # 搜索栏
│   │   │   ├── MagnetSubmit.vue     # 磁力提交
│   │   │   ├── HotResources.vue     # 热门排行
│   │   │   ├── ResourceDetail.vue   # 资源详情
│   │   │   └── CopyrightPanel.vue   # 版权管理面板 (管理员)
│   │   ├── api/
│   │   │   └── index.js             # API 客户端
│   │   ├── App.vue                  # 主布局 (含管理员登录/侵权统计)
│   │   ├── main.js
│   │   └── styles.css
│   ├── index.html
│   ├── package.json
│   └── vite.config.js
└── scripts/
    ├── start.bat              # Windows 启动
    └── start.sh               # Linux/Mac 启动
```

## 快速开始

### 前置依赖

- **C++**: CMake >= 3.16, libtorrent-rasterbar-dev, libboost-system-dev, libssl-dev, libcpr-dev
- **Python**: Python >= 3.8, Flask, flask-cors, libtorrent, Pillow (可选，用于 pHash), numpy (可选，用于 DCT)
- **Node**: Node.js >= 16

### 启动 Flask API

```bash
cd backend/api
pip install -r requirements.txt
python app.py
# 访问 http://127.0.0.1:5000
```

### 带指纹库启动

```bash
python app.py --fingerprint-db /path/to/copyright_fingerprints.json --admin-token your-secret-token
```

### 编译 DHT 爬虫

```bash
cd backend/crawler
mkdir build && cd build
cmake ..
cmake --build .
./bt_crawler --crawl-interval 60 --listen-port 6881 --dht-port 6882
```

### 启动前端开发模式

```bash
cd frontend
npm install
npm run dev
# 访问 http://localhost:3000
```

### 构建前端

```bash
cd frontend
npm run build
# 产物在 frontend/dist/
```

## 配置参数

### C++ 爬虫 (config.h)

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `bloom_filter_size` | 10000000 | 布隆过滤器位数 |
| `bloom_filter_hash_count` | 7 | 哈希函数数量 |
| `blacklist_ttl_seconds` | 86400 | 黑名单过期时间(秒) |
| `metadata_timeout_seconds` | 15 | Metadata 下载超时(秒) |
| `max_retry_count` | 2 | 最大重试次数 |
| `failure_rate_threshold` | 0.5 | 触发 Bootstrap 切换的失败率阈值 |
| `bootstrap_node_list` | 8个节点 | Bootstrap 节点列表 |

### Python 端 (database.py 类常量)

| 常量 | 值 | 说明 |
|------|-----|------|
| `BLACKLIST_TTL_SECONDS` | 86400 | 黑名单 24 小时过期 |
| `METADATA_TIMEOUT` | 15 | Metadata 下载 15 秒超时 |
| `MAX_RETRY` | 2 | 最多 2 次重试 |
| `FAILURE_RATE_THRESHOLD` | 0.5 | 50% 失败率阈值 |
| `COPYRIGHT_THRESHOLD` | 0.95 | 95% 相似度判定侵权 |

## 命令行参数 (Python API)

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 0.0.0.0 | 绑定地址 |
| `--port` | 5000 | 绑定端口 |
| `--crawl-interval` | 60 | 爬虫间隔（秒） |
| `--fingerprint-db` | data/copyright_fingerprints.json | 版权指纹库路径 |
| `--admin-token` | admin-secret-change-me | 管理员 API 令牌 |

## 命令行参数 (C++ 爬虫)

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--crawl-interval` | 60 | 爬虫间隔（秒） |
| `--listen-port` | 6881 | 监听端口 |
| `--dht-port` | 6882 | DHT 端口 |
| `--api-endpoint` | http://127.0.0.1:5000/api/infohash | API 上报地址 |
| `--db-path` | ../data/bt_monitor.db | 数据库路径 |
| `--max-infohash` | 500 | 每周期最大收集数 |

## API 接口

### 资源管理

- `GET /api/resources` - 分页获取资源列表（侵权资源对普通用户隐藏）
  - Query: `page`, `per_page`, `status` (pending/parsed/invalid)
- `GET /api/resources/<infohash>` - 获取资源详情（含侵权标记）
- `POST /api/infohash` - 上报 infohash (爬虫内部使用)
  - Body: `{infohash, source_ip, source_port, timestamp, event_type, announce_count}`

### 搜索

- `GET /api/search?q=<关键词>` - FTS5 全文搜索（侵权资源对普通用户隐藏）

### 热门资源

- `GET /api/hot?hours=24&limit=100` - 热门资源排行（侵权资源对普通用户隐藏）

### 磁力提交

- `POST /api/magnet/submit` - 提交磁力链接（自动版权检测）
  - Body: `{magnet_uri, fetch_metadata: false}`
  - 响应：已在黑名单的资源返回 410 Gone

### 统计

- `GET /api/stats` - 系统统计（含版权统计数据）

### 健康度

- `POST /api/health/<infohash>` - 更新种子健康度
  - Body: `{seeders, leechers}`

### 黑名单管理

- `POST /api/blacklist/cleanup` - 清理过期黑名单并重置失败统计

### 版权管理（需 `X-Admin-Token` 请求头）

- `GET /api/copyright/stats` - 版权检测统计（按版权方汇总）
- `GET /api/copyright/infringements` - 侵权记录列表
  - Query: `status` (suspected/confirmed/false_positive/resolved), `holder` (版权方), `limit`
- `PUT /api/copyright/infringements/<id>/status` - 更新侵权记录状态
  - Body: `{status: "confirmed|false_positive|suspected|resolved"}`
- `GET /api/copyright/fingerprints` - 获取所有版权指纹
- `POST /api/copyright/fingerprints/import` - 导入版权指纹
  - Body: `{fingerprints: [{phash, title, copyright_holder, ...}]}`
- `POST /api/copyright/dmca/generate` - 为单条记录生成 DMCA 通知
  - Body: `{record_id, recipient_email, sender_name, sender_email}`
- `POST /api/copyright/dmca/batch` - 批量生成 DMCA 通知
  - Body: `{copyright_holder: "Holder Name"}` 或 `{record_ids: [1,2,3]}`
- `GET /api/copyright/dmca` - 获取 DMCA 通知列表
- `POST /api/copyright/dmca/<id>/send` - 标记 DMCA 通知为已发送

## 数据库 Schema

### infohashes 表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK | 主键 |
| infohash | TEXT UNIQUE | 40 位十六进制哈希 |
| source_ip | TEXT | 来源 IP |
| source_port | INTEGER | 来源端口 |
| first_seen | INTEGER | 首次发现时间戳 |
| last_seen | INTEGER | 最近发现时间戳 |
| download_count | INTEGER | 下载计数 |
| seeders | INTEGER | 做种数 |
| leechers | INTEGER | 下载数 |
| estimated_seeders | INTEGER | 估算做种数（用于智能排序） |
| retry_count | INTEGER | 已重试次数 |
| last_attempt | INTEGER | 最近尝试时间 |
| status | TEXT | pending/parsed/invalid |

### torrents 表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK | 主键 |
| infohash | TEXT UNIQUE FK | 关联 infohash |
| name | TEXT | 资源名称 |
| total_size | INTEGER | 总大小（字节） |
| piece_length | INTEGER | 分片长度 |
| piece_count | INTEGER | 分片数量 |
| piece_hashes | TEXT JSON | 分片哈希数组 |
| files | TEXT JSON | 文件列表 |
| parsed_at | INTEGER | 解析时间 |

### blacklist 表

| 字段 | 类型 | 说明 |
|------|------|------|
| infohash | TEXT PK | 被拉黑的 infohash |
| reason | TEXT | 拉黑原因 (metadata_timeout / max_retries_exceeded / api_blacklisted) |
| added_at | INTEGER | 加入时间 |
| expires_at | INTEGER | 过期时间 (added_at + 86400) |

### copyright_fingerprints 表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK | 主键 |
| md5 | TEXT UNIQUE | MD5 哈希 |
| sha1 | TEXT | SHA1 哈希 |
| sha256 | TEXT | SHA256 哈希 |
| phash | TEXT | 感知哈希 (pHash) |
| file_size | INTEGER | 文件大小 |
| filename | TEXT | 原始文件名 |
| title | TEXT | 作品标题 |
| copyright_holder | TEXT | 版权方名称 |
| original_source | TEXT | 原始来源 |
| added_at | INTEGER | 添加时间 |
| fingerprint_version | TEXT | 指纹版本 |

### infringement_records 表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK | 主键 |
| infohash | TEXT NOT NULL FK | 侵权资源 infohash |
| file_name | TEXT | 文件名 |
| file_size | INTEGER | 文件大小 |
| phash | TEXT | 感知哈希 |
| matched_fingerprint_id | INTEGER FK | 匹配的版权指纹 ID |
| matched_title | TEXT | 匹配的作品标题 |
| copyright_holder | TEXT | 版权方 |
| similarity | REAL | 相似度 (0-1) |
| uploader_ip | TEXT | 上传者 IP（如可获取） |
| source_ip | TEXT | 来源 IP |
| detected_at | INTEGER | 检测时间 |
| status | TEXT | suspected/confirmed/false_positive/resolved |
| dmca_generated | INTEGER | 是否已生成 DMCA (0/1) |
| evidence_json | TEXT JSON | 完整证据数据 |

### dmca_notices 表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER PK | 主键 |
| infringement_id | INTEGER FK | 关联侵权记录 |
| infohash | TEXT | 资源 infohash |
| recipient_email | TEXT | 收件人邮箱 |
| subject | TEXT | 邮件主题 |
| notice_text | TEXT | DMCA 通知全文 |
| generated_at | INTEGER | 生成时间 |
| sent_at | INTEGER | 发送时间 |
| status | TEXT | generated/sent |

### torrent_fts (FTS5 虚拟表)

- 对 `name` 和 `files` 字段建立全文索引
- 使用 porter 分词器 + unicode61 支持中文

### download_history 表

- 记录每次下载事件，用于热门资源统计

## License

MIT
