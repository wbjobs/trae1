# CDN缓存防击穿系统

基于Go + Redis + Bloom Filter实现的CDN缓存防击穿系统。

## 功能特性

- **热点Key检测**：自动识别每分钟请求超过1000次的热点Key
- **Bloom Filter预过滤**：使用Bloom Filter预先记录热点Key，非热点Key直接回源
- **Redis缓存**：缓存热点Key的响应数据
- **Singleflight互斥锁**：缓存未命中时只让一个请求回源，其他请求等待
- **管理API**：提供API查看热点Key列表和Bloom Filter误判率

## 系统架构

```
请求 → Bloom Filter检查 → 不存在 → 直接回源
                      → 存在 → Redis缓存查询 → 命中 → 返回
                                          → 未命中 → Singleflight → 单个回源 → 缓存 → 返回
```

## 项目结构

```
├── cmd/server/           # 主程序入口
│   └── main.go
├── internal/
│   ├── api/              # 管理API
│   │   └── api.go
│   ├── bloom/            # Bloom Filter实现
│   │   └── bloom.go
│   ├── cache/            # Redis缓存层
│   │   └── cache.go
│   ├── detector/         # 热点Key检测器
│   │   └── detector.go
│   ├── handler/          # HTTP请求处理器
│   │   └── handler.go
│   ├── model/            # 数据模型
│   │   └── model.go
│   └── singleflight/     # 互斥锁模式
│       └── singleflight.go
├── go.mod
└── go.sum
```

## 环境要求

- Go 1.21+
- Redis 6.0+

## 安装与运行

```bash
# 下载依赖
go mod tidy

# 编译
go build -o cdn-cache-protector ./cmd/server

# 运行
./cdn-cache-protector
```

## 环境变量

| 变量名 | 默认值 | 说明 |
|--------|--------|------|
| REDIS_ADDR | localhost:6379 | Redis地址 |
| REDIS_PASSWORD | 无 | Redis密码 |
| SERVER_ADDR | :8080 | 服务监听地址 |

## API接口

### 缓存访问

```
GET /cache?key=xxx
```

### 管理API

| 接口 | 方法 | 说明 |
|------|------|------|
| /api/stats | GET | 系统统计信息 |
| /api/hotkeys | GET | 热点Key列表 |
| /api/bloom/stats | GET | Bloom Filter统计 |
| /api/bloom/reset | POST | 重置Bloom Filter |

### 响应示例

**GET /api/stats**

```json
{
  "hot_keys": [
    {
      "key": "hot-key-1",
      "count": 1500,
      "window_start": "2024-01-01T00:00:00Z",
      "is_hot": true
    }
  ],
  "bloom_stats": {
    "total_requests": 10000,
    "bloom_hits": 8500,
    "bloom_misses": 1500,
    "false_positives": 85,
    "false_positive_rate": 0.01,
    "estimated_size": 500
  },
  "total_requests": 10000,
  "cache_hits": 7000,
  "cache_misses": 1500,
  "origin_fetches": 1500,
  "cache_hit_rate": 0.7
}
```

## 核心配置

- **Bloom Filter**：预期100,000个元素，误判率1%
- **热点Key阈值**：每分钟超过1000次请求
- **缓存TTL**：300秒
